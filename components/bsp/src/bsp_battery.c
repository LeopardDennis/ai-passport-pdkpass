// components/bsp/src/bsp_battery.c
// 移植自 trae_card/components/platform/platform_esp32/src/battery_cw2017.c
// 使用官方原装电芯 profile；更换电芯时必须重新匹配厂家参数。
#include "bsp_battery.h"
#include <stdbool.h>
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_batt";

#define CW_REG_VERSION   0x00   // 版本号,上电应答即代表芯片在位
#define CW_REG_VCELL_H   0x02   // 14bit 电压,V(uV) = raw * 312.5
#define CW_REG_SOC_H     0x04   // 高字节 = 整数百分比;低字节(0x05)= 1/256 %
#define CW_REG_CONFIG    0x08   // 0xF0=睡眠 / 0x30=复位态 / 0x00=正常

#define CW_REG_SOC_ALERT 0x0B
#define CW_REG_PROFILE   0x10
#define CW_UPDATE_FLAG   0x80
#define CW_PROFILE_SIZE  80

// Original-cell vendor profile from FoloToy/ai-passport commit
// a06235e4c94361e3f8a2d498930254101718acae. Do not use for replacement cells
// without matching supplier parameters and charge/discharge validation.
static const uint8_t s_battery_profile[CW_PROFILE_SIZE] = {
    0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAD, 0xC7, 0xC8, 0xCA, 0xBD, 0xB1, 0xC1, 0x94,
    0x88, 0xD1, 0xBD, 0x97, 0x88, 0x66, 0x56, 0x4A,
    0x3F, 0x33, 0x26, 0x5C, 0x37, 0xD1, 0x27, 0xD8,
    0xCC, 0xB7, 0xCF, 0xB3, 0xB2, 0xAE, 0xA6, 0x9E,
    0x99, 0x97, 0x9B, 0x86, 0x47, 0x1E, 0x17, 0x26,
    0x49, 0x96, 0xD9, 0xE1, 0xDD, 0xDC, 0xD4, 0x59,
    0x00, 0x00, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C,
};
_Static_assert(sizeof(s_battery_profile) == CW_PROFILE_SIZE,
               "CW2017 battery profile must contain exactly 80 bytes");

#define CW_INIT_RETRY_US (60LL * 1000000LL)

// Serialized by the caller; failed initialization retries on visible SOC requests.
static i2c_master_dev_handle_t s_dev;
static int64_t s_init_retry_at_us;

static int cw_read(uint8_t reg, uint8_t *buf, size_t n) {
    if (!s_dev) return -1;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100) == ESP_OK ? 0 : -1;
}

static int cw_write(uint8_t reg, uint8_t val) {
    if (!s_dev) return -1;
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK ? 0 : -1;
}

// CW2017-DS V1.1, Power State / CONFIG (0x08). This restarts the
// gauge using its existing profile; it does not change cell parameters.
static esp_err_t cw_restart(void) {
    uint8_t config;
    if (cw_write(CW_REG_CONFIG, 0x30) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (cw_write(CW_REG_CONFIG, 0x00) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(100));
    if (cw_read(CW_REG_CONFIG, &config, 1) != 0 ||
        (config & 0xF0U) != 0) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t cw_ensure_active(void) {
    uint8_t config;
    if (cw_read(CW_REG_CONFIG, &config, 1) != 0) return ESP_FAIL;
    // Preserve the estimate of an already-running gauge on MCU reboot.
    return (config & 0xF0U) == 0 ? ESP_OK : cw_restart();
}

static esp_err_t cw_profile_matches(bool *matches) {
    uint8_t value;
    *matches = false;
    if (cw_read(CW_REG_SOC_ALERT, &value, 1) != 0) return ESP_FAIL;
    if ((value & CW_UPDATE_FLAG) == 0) return ESP_OK;
    for (unsigned i = 0; i < CW_PROFILE_SIZE; ++i) {
        if (cw_read(CW_REG_PROFILE + i, &value, 1) != 0) return ESP_FAIL;
        if (value != s_battery_profile[i]) return ESP_OK;
    }
    *matches = true;
    return ESP_OK;
}

static esp_err_t cw_update_profile(void) {
    uint8_t value, alert;
    if (cw_write(CW_REG_CONFIG, 0x30) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (cw_write(CW_REG_CONFIG, 0xF0) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (cw_read(CW_REG_CONFIG, &value, 1) != 0 || value != 0xF0)
        return ESP_FAIL;
    if (cw_read(CW_REG_SOC_ALERT, &alert, 1) != 0) return ESP_FAIL;
    // A failed/partial update must not leave the profile marked as complete.
    alert &= ~CW_UPDATE_FLAG;
    if (cw_write(CW_REG_SOC_ALERT, alert) != 0) return ESP_FAIL;
    if (cw_read(CW_REG_SOC_ALERT, &value, 1) != 0 || value != alert)
        return ESP_FAIL;
    for (unsigned i = 0; i < CW_PROFILE_SIZE; ++i) {
        if (cw_write(CW_REG_PROFILE + i, s_battery_profile[i]) != 0)
            return ESP_FAIL;
    }
    for (unsigned i = 0; i < CW_PROFILE_SIZE; ++i) {
        if (cw_read(CW_REG_PROFILE + i, &value, 1) != 0 ||
            value != s_battery_profile[i]) return ESP_FAIL;
    }
    alert |= CW_UPDATE_FLAG;
    if (cw_write(CW_REG_SOC_ALERT, alert) != 0) return ESP_FAIL;
    if (cw_read(CW_REG_SOC_ALERT, &value, 1) != 0 || value != alert)
        return ESP_FAIL;
    return cw_restart();
}

static esp_err_t cw_wait_soc_ready(void) {
    for (unsigned retry = 0; retry < 50; ++retry) {
        uint8_t soc[2];
        vTaskDelay(pdMS_TO_TICKS(100));
        if (cw_read(CW_REG_SOC_H, soc, sizeof(soc)) != 0) return ESP_FAIL;
        if (soc[0] <= 100) return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t bsp_battery_init(void) {
    if (s_dev) return ESP_OK;
    s_init_retry_at_us = esp_timer_get_time() + CW_INIT_RETRY_US;

    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) return e;

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_I2C_CW2017_ADDR,
        .scl_speed_hz    = 100000,
    };
    e = i2c_master_bus_add_device(bsp_i2c_bus(), &dc, &s_dev);
    if (e != ESP_OK) { ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(e)); return e; }

    uint8_t ver = 0;
    if (cw_read(CW_REG_VERSION, &ver, 1) != 0) {
        ESP_LOGW(TAG, "CW2017 未应答，检查 0x%02X 是否在线;"
                      "无电量计的板子可忽略本项", BSP_I2C_CW2017_ADDR);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    bool matches = false;
    e = cw_profile_matches(&matches);
    if (e == ESP_OK) {
        if (matches) {
            e = cw_ensure_active();
        } else {
            e = cw_update_profile();
        }
    }
    if (e == ESP_OK) e = cw_wait_soc_ready();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "CW2017 profile/mode/SOC initialization failed: %s", esp_err_to_name(e));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return e;
    }
    return ESP_OK;
}

int bsp_battery_soc(void) {
    int64_t now = esp_timer_get_time();
    if (!s_dev && (now < s_init_retry_at_us || bsp_battery_init() != ESP_OK))
        return -1;

    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_SOC_H, b, 2) != 0) return -1;
    // Invalid runtime samples remain unavailable; do not periodically reset
    // an already-initialized gauge or invent SOC from voltage.
    return b[0] <= 100 ? b[0] : -1;
}

int bsp_battery_mv(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_VCELL_H, b, 2) != 0) return -1;
    uint32_t raw = ((uint32_t)b[0] << 8 | b[1]) & 0x3FFF;   // 14bit
    return (int)((raw * 3125) / 10000);                     // raw * 312.5uV → mV
}
