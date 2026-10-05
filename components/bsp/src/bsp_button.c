#include "bsp_button.h"
#include "bsp_pins.h"
#include "iot_button.h"
#include "button_interface.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_intr_alloc.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_btn";
static const uint16_t BTN_MV[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;
#define BSP_BTN_ATTEN ADC_ATTEN_DB_12

typedef struct {
    button_driver_t base;
    unsigned index;
} ladder_driver_t;

static ladder_driver_t s_driver[BSP_BTN_COUNT];
static button_handle_t s_btn[BSP_BTN_COUNT];
static bsp_btn_cb_t s_cb;
static void *s_user;
static SemaphoreHandle_t s_lock;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_initialized, s_parked, s_wakeup_available;
static int s_sample_mv = -1;
static int64_t s_sample_at;

// ADC acquisition/teardown and explicit diagnostic reads share this mutex.
// Button callbacks remain on the component task, never in ISR context.
static esp_err_t open_adc_locked(void)
{
    if (s_adc) return ESP_OK;
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = BSP_BTN_ADC_UNIT};
    esp_err_t err = adc_oneshot_new_unit(&unit, &s_adc);
    if (err != ESP_OK) return err;
    const adc_oneshot_chan_cfg_t channel = {
        .atten = BSP_BTN_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, BSP_BTN_ADC_CHANNEL, &channel);
    if (err != ESP_OK && adc_oneshot_del_unit(s_adc) == ESP_OK) s_adc = NULL;
    s_sample_mv = -1;
    return err;
}

static esp_err_t disable_wakeup_locked(void)
{
    esp_err_t err = gpio_intr_disable(BSP_BTN_GPIO);
    if (err == ESP_OK) err = gpio_wakeup_disable(BSP_BTN_GPIO);
    s_parked = false;
    s_sample_mv = -1;
    return err;
}

static esp_err_t park_locked(void)
{
    if (s_parked) return ESP_OK;
    if (s_adc) {
        esp_err_t err = adc_oneshot_del_unit(s_adc);
        if (err != ESP_OK) return err;
        s_adc = NULL;
    }
    // Nominal pressed levels (0/300/595 mV) are below C3's VIL maximum,
    // 0.25*VDD (825 mV at 3.3 V). The external 10k pull-up keeps idle high.
    // Internal pulls must stay disabled to preserve the ladder voltages.
    const gpio_config_t pin = {
        .pin_bit_mask = UINT64_C(1) << BSP_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&pin);
    if (err == ESP_OK) err = gpio_set_intr_type(BSP_BTN_GPIO, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) err = gpio_wakeup_enable(BSP_BTN_GPIO, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) {
        s_parked = true;
        s_sample_mv = -1;
        err = gpio_intr_enable(BSP_BTN_GPIO);
    }
    if (err != ESP_OK) disable_wakeup_locked();
    return err;
}

static void IRAM_ATTR ladder_wakeup_isr(void *unused)
{
    (void)unused;
    // Masks the interrupt and resumes the timer; ADC restoration happens in
    // task context through the component's pending-wakeup coordination.
    iot_button_power_save_wakeup_isr(BSP_BTN_GPIO);
}

static esp_err_t ladder_enter_idle(button_driver_t *driver)
{
    (void)driver;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = park_locked();
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        // The component already stopped polling. Resume if arming failed,
        // so a failed wake-source setup cannot make all keys inaccessible.
        ESP_LOGW(TAG, "Button idle failed: %s", esp_err_to_name(err));
        iot_button_resume();
    }
    return err;
}

static esp_err_t ladder_exit_idle(button_driver_t *driver)
{
    (void)driver;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = disable_wakeup_locked();
    xSemaphoreGive(s_lock);
    return err;
}

static int32_t ladder_gpio(button_driver_t *driver)
{
    (void)driver;
    return BSP_BTN_GPIO;
}

// Preserve multisampling/windows and share one sample between logical keys.
// Failed ADC reads must never synthesize an UP press.
static int read_mv_locked(void)
{
    if (open_adc_locked() != ESP_OK) return -1;
    int sum = 0;
    for (unsigned i = 0; i < CONFIG_ADC_BUTTON_SAMPLE_TIMES; i++) {
        int raw;
        if (adc_oneshot_read(s_adc, BSP_BTN_ADC_CHANNEL, &raw) != ESP_OK) return -1;
        sum += raw;
    }
    int mv;
    if (adc_cali_raw_to_voltage(s_cali, sum / CONFIG_ADC_BUTTON_SAMPLE_TIMES, &mv) != ESP_OK)
        return -1;
    return mv;
}

static uint8_t ladder_level(button_driver_t *driver)
{
    const ladder_driver_t *key = (const ladder_driver_t *)driver;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // A failed idle attempt may have resumed the timer before another logical
    // key successfully armed wakeup. Any timer-driven ADC read must unpark,
    // even when there is no pending ISR notification for this scan.
    if (s_parked) disable_wakeup_locked();
    int64_t now = esp_timer_get_time();
    if (s_sample_mv < 0 || now - s_sample_at > 1000) {
        s_sample_mv = read_mv_locked();
        s_sample_at = now;
    }
    int mv = s_sample_mv;
    xSemaphoreGive(s_lock);
    return mv >= BTN_MV[key->index][0] && mv <= BTN_MV[key->index][1];
}

static esp_err_t ladder_delete(button_driver_t *driver)
{
    (void)driver;
    // Logical drivers are static and share hardware owned by the BSP.
    return ESP_OK;
}

static void on_event(void *unused, void *user, bsp_btn_ev_t event)
{
    (void)unused;
    if (s_cb) s_cb((bsp_btn_t)(intptr_t)user, event, s_user);
}
static void cb_press(void *a, void *u) { on_event(a, u, BSP_BTN_PRESS); }
static void cb_click(void *a, void *u) { on_event(a, u, BSP_BTN_CLICK); }
static void cb_double(void *a, void *u) { on_event(a, u, BSP_BTN_DOUBLE); }
static void cb_long(void *a, void *u) { on_event(a, u, BSP_BTN_LONG); }

esp_err_t bsp_button_init(bsp_btn_cb_t cb, void *user)
{
    s_cb = cb;
    s_user = user;
    if (s_initialized) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    const adc_cali_curve_fitting_config_t cal = {
        .unit_id = BSP_BTN_ADC_UNIT, .chan = BSP_BTN_ADC_CHANNEL,
        .atten = BSP_BTN_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cal, &s_cali);
    if (err != ESP_OK) goto fail;
    err = open_adc_locked();
    if (err != ESP_OK) goto fail;

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
        err = gpio_isr_handler_add(BSP_BTN_GPIO, ladder_wakeup_isr, NULL);
    s_wakeup_available = err == ESP_OK;
    if (s_wakeup_available) {
        err = esp_sleep_enable_gpio_wakeup();
        if (err != ESP_OK) {
            gpio_isr_handler_remove(BSP_BTN_GPIO);
            s_wakeup_available = false;
        }
    }
    if (!s_wakeup_available)
        ESP_LOGW(TAG, "Button wake unavailable; retaining continuous polling");

    for (unsigned i = 0; i < BSP_BTN_COUNT; i++) {
        s_driver[i] = (ladder_driver_t){
            .index = i,
            .base = {
                // Keep the timer suspended until all keys/callbacks exist,
                // including when startup will fall back to polling.
                .enable_power_save = true,
                .get_key_level = ladder_level,
                .enter_power_save = ladder_enter_idle,
                .exit_power_save = ladder_exit_idle,
                .get_gpio_num = ladder_gpio,
                .del = ladder_delete,
            },
        };
        const button_config_t config = {0};
        err = iot_button_create(&config, &s_driver[i].base, &s_btn[i]);
        if (err != ESP_OK) goto fail;
        void *index = (void *)(intptr_t)i;
        const button_event_t events[] = {BUTTON_PRESS_DOWN, BUTTON_SINGLE_CLICK,
                                        BUTTON_DOUBLE_CLICK, BUTTON_LONG_PRESS_START};
        const button_cb_t callbacks[] = {cb_press, cb_click, cb_double, cb_long};
        for (unsigned j = 0; j < sizeof(events) / sizeof(events[0]); j++) {
            err = iot_button_register_cb(s_btn[i], events[j], NULL, callbacks[j], index);
            if (err != ESP_OK) goto fail;
        }
    }
    for (unsigned i = 0; i < BSP_BTN_COUNT; i++)
        s_driver[i].base.enable_power_save = s_wakeup_available;
    // Detect keys held during startup, before an interrupt has been armed.
    err = iot_button_resume();
    if (err != ESP_OK) goto fail;
    s_initialized = true;
    return ESP_OK;
fail:
    for (unsigned i = 0; i < BSP_BTN_COUNT; i++) {
        if (s_btn[i]) iot_button_delete(s_btn[i]);
        s_btn[i] = NULL;
    }
    if (s_wakeup_available) gpio_isr_handler_remove(BSP_BTN_GPIO);
    s_wakeup_available = false;
    disable_wakeup_locked();
    if (s_adc && adc_oneshot_del_unit(s_adc) == ESP_OK) s_adc = NULL;
    if (s_cali) adc_cali_delete_scheme_curve_fitting(s_cali);
    s_cali = NULL;
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    ESP_LOGE(TAG, "Button initialization failed: %s", esp_err_to_name(err));
    return err;
}

int bsp_button_read_mv(void)
{
    if (!s_initialized) return -1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool parked = s_parked;
    if (parked) disable_wakeup_locked();
    int mv = read_mv_locked();
    esp_err_t err = parked ? park_locked() : ESP_OK;
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) iot_button_resume();
    return mv;
}
