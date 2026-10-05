"""Run the production BSP with the actual button component and fake peripherals."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def without_includes(path):
    return re.sub(r'^\s*#(?:include|pragma once)[^\n]*$', '', path.read_text(), flags=re.M)

class ButtonPowerTests(unittest.TestCase):
    def test_gestures_gpio_wake_adc_ownership_and_fallback(self):
        component = ROOT / 'managed_components/espressif__button'
        if not (component/'iot_button.c').is_file():
            if os.environ.get('PDKPASS_REQUIRE_BUTTON_COMPONENT') == '1':
                self.fail('Firmware gate did not resolve the pinned button component')
            self.skipTest('Requires resolved button component; firmware gate reruns this test')
        common = r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define IRAM_ATTR
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_RETURN_ON_FALSE(a,ret,...) do {if(!(a)) return (ret);} while(0)
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define CONFIG_BUTTON_PERIOD_TIME_MS 20
#define CONFIG_BUTTON_DEBOUNCE_TICKS 2
#define CONFIG_BUTTON_SHORT_PRESS_TIME_MS 180
#define CONFIG_BUTTON_LONG_PRESS_TIME_MS 1500
#define CONFIG_BUTTON_LONG_PRESS_HOLD_SERIAL_TIME_MS 20
#define CONFIG_ADC_BUTTON_SAMPLE_TIMES 1
#define BUTTON_VER_MAJOR 4
#define BUTTON_VER_MINOR 2
#define BUTTON_VER_PATCH 0
#define BSP_BTN_COUNT 3
#define BSP_BTN_GPIO 0
#define BSP_BTN_ADC_UNIT 1
#define BSP_BTN_ADC_CHANNEL 0
#define BSP_BTN_MV_TABLE {{0,150},{150,447},{447,1900}}
#define ADC_ATTEN_DB_12 12
#define ADC_BITWIDTH_DEFAULT 12
#define ESP_INTR_FLAG_IRAM 1
#define portMAX_DELAY 0xffffffff
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_LOW_LEVEL 1
#define ESP_TIMER_TASK 0
struct fake_timer {bool running;void(*callback)(void*);void*arg;};
typedef struct fake_timer *esp_timer_handle_t;
typedef struct {void*arg;void(*callback)(void*);int dispatch_method;const char*name;} esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
int esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*);
int esp_timer_start_periodic(esp_timer_handle_t,uint64_t);
int esp_timer_stop(esp_timer_handle_t);
int esp_timer_delete(esp_timer_handle_t);
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,uint32_t);
void xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
typedef int *adc_oneshot_unit_handle_t;
typedef int *adc_cali_handle_t;
typedef struct {int unit_id;} adc_oneshot_unit_init_cfg_t;
typedef struct {int atten,bitwidth;} adc_oneshot_chan_cfg_t;
typedef struct {int unit_id,chan,atten,bitwidth;} adc_cali_curve_fitting_config_t;
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*,adc_oneshot_unit_handle_t*);
int adc_oneshot_config_channel(adc_oneshot_unit_handle_t,int,const adc_oneshot_chan_cfg_t*);
int adc_oneshot_del_unit(adc_oneshot_unit_handle_t);
int adc_oneshot_read(adc_oneshot_unit_handle_t,int,int*);
int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*,adc_cali_handle_t*);
int adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t);
int adc_cali_raw_to_voltage(adc_cali_handle_t,int,int*);
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
int gpio_config(const gpio_config_t*);
int gpio_set_intr_type(int,int);
int gpio_intr_disable(int);
int gpio_intr_enable(int);
int gpio_wakeup_enable(int,int);
int gpio_wakeup_disable(int);
int gpio_install_isr_service(int);
int gpio_isr_handler_add(int,void(*)(void*),void*);
int gpio_isr_handler_remove(int);
int esp_sleep_enable_gpio_wakeup(void);
const char* esp_err_to_name(int);
'''
        for path in [component/'interface/button_interface.h', component/'include/button_types.h',
                     component/'include/iot_button.h', ROOT/'components/bsp/include/bsp_button.h']:
            common += without_includes(path)
        fake = r'''
#include "common.h"
static int64_t now_us;
static int physical_mv=3300,adc_live,adc_opens,adc_reads,fail_kind,irq_count;
static bool irq_enabled,wake_enabled;
static void(*isr)(void*);
static struct fake_timer *timer;
static int counts[3][4];
static bool failure(int kind) {if(fail_kind==kind){fail_kind=0;return true;}return false;}
const char*esp_err_to_name(int e) {(void)e;return "fake";}
int64_t esp_timer_get_time(void) {return now_us;}
int esp_timer_create(const esp_timer_create_args_t*a,esp_timer_handle_t*out) {
 timer=calloc(1,sizeof(*timer));timer->callback=a->callback;timer->arg=a->arg;*out=timer;return 0;
}
int esp_timer_start_periodic(esp_timer_handle_t t,uint64_t p) {assert(p==20000);t->running=true;return 0;}
int esp_timer_stop(esp_timer_handle_t t) {t->running=false;return 0;}
int esp_timer_delete(esp_timer_handle_t t) {free(t);timer=NULL;return 0;}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return calloc(1,sizeof(int));}
int xSemaphoreTake(SemaphoreHandle_t s,uint32_t wait) {(void)wait;assert(s&&!*s);*s=1;return 1;}
void xSemaphoreGive(SemaphoreHandle_t s) {assert(*s==1);*s=0;}
void vSemaphoreDelete(SemaphoreHandle_t s) {assert(!*s);free(s);}
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*c,adc_oneshot_unit_handle_t*out) {
 assert(c->unit_id==1&&!adc_live);if(failure(1))return -1;*out=malloc(sizeof(int));adc_live++;adc_opens++;return 0;
}
int adc_oneshot_config_channel(adc_oneshot_unit_handle_t h,int ch,const adc_oneshot_chan_cfg_t*c) {
 assert(h&&ch==0&&c->atten==12);return failure(2)?-1:0;
}
int adc_oneshot_del_unit(adc_oneshot_unit_handle_t h) {assert(h&&adc_live==1);if(failure(3))return -1;free(h);adc_live--;return 0;}
int adc_oneshot_read(adc_oneshot_unit_handle_t h,int ch,int*out) {assert(h&&ch==0&&adc_live);adc_reads++;if(failure(4))return -1;*out=physical_mv;return 0;}
int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*c,adc_cali_handle_t*out) {
 assert(c->unit_id==1&&c->chan==0&&c->atten==12);if(failure(5))return -1;*out=malloc(sizeof(int));return 0;
}
int adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t h) {free(h);return 0;}
int adc_cali_raw_to_voltage(adc_cali_handle_t h,int raw,int*out) {assert(h);if(failure(6))return -1;*out=raw;return 0;}
int gpio_config(const gpio_config_t*c) {
 assert(!adc_live&&c->pin_bit_mask==1&&c->mode==1&&!c->pull_up_en&&!c->pull_down_en);return failure(7)?-1:0;
}
int gpio_set_intr_type(int pin,int type) {assert(pin==0&&type==1);return 0;}
int gpio_intr_disable(int pin) {assert(pin==0);irq_enabled=false;return 0;}
int gpio_intr_enable(int pin) {
 assert(pin==0);if(failure(8))return -1;irq_enabled=true;
 if(physical_mv<825&&isr){irq_count++;isr(NULL);}return 0;
}
int gpio_wakeup_enable(int pin,int type) {assert(pin==0&&type==1);if(failure(9))return -1;wake_enabled=true;return 0;}
int gpio_wakeup_disable(int pin) {assert(pin==0);wake_enabled=false;return 0;}
int gpio_install_isr_service(int flag) {assert(flag==1);return failure(10)?-1:0;}
int gpio_isr_handler_add(int pin,void(*cb)(void*),void*arg) {assert(pin==0&&!arg);isr=cb;return 0;}
int gpio_isr_handler_remove(int pin) {assert(pin==0);isr=NULL;return 0;}
int esp_sleep_enable_gpio_wakeup(void) {return failure(11)?-1:0;}
static void sample_ticks(unsigned n) {
 while(n--){now_us+=20000;if(timer&&timer->running)timer->callback(timer->arg);}
}
static void physical(int mv) {
 physical_mv=mv;if(mv<825&&irq_enabled&&isr){irq_count++;isr(NULL);}
}
static void event(bsp_btn_t key,bsp_btn_ev_t ev,void*user) {assert(user==(void*)42);counts[key][ev]++;}
int main(int argc,char**argv) {
 const int voltages[]={0,300,595};
 if(argc>1)fail_kind=atoi(argv[1]);
 int mode=fail_kind;
 if(mode==12){fail_kind=0;physical_mv=595;} // A key held before wake has ever been armed.
 int status=bsp_button_init(event,(void*)42);
 if(mode==1||mode==2||mode==5){assert(status!=0&&!adc_live);assert(bsp_button_init(event,(void*)42)==0);}
 else assert(status==0);
 assert(bsp_button_init(event,(void*)42)==0); // Idempotent, no duplicate unit/keys.
 if(mode==12) {
  sample_ticks(4);assert(counts[2][BSP_BTN_PRESS]==1);
  physical(3300);sample_ticks(20);assert(counts[2][BSP_BTN_CLICK]==1);
  memset(counts,0,sizeof(counts));
 }
 sample_ticks(1);
 bool fallback=mode==10||mode==11;
 if(fallback)assert(timer->running&&adc_live==1&&!isr);
 else assert(!timer->running&&!adc_live&&wake_enabled&&irq_enabled);
 int reads=adc_reads,opens=adc_opens;sample_ticks(1000);
 if(!fallback)assert(adc_reads==reads&&adc_opens==opens); // No periodic standby ADC/timer work.
 for(int key=0;key<3;key++) {
  physical(voltages[key]);sample_ticks(4);assert(counts[key][BSP_BTN_PRESS]==1);
  physical(3300);sample_ticks(20);assert(counts[key][BSP_BTN_CLICK]==1);
  if(!fallback)assert(!timer->running&&!adc_live&&wake_enabled&&irq_enabled);
  physical(voltages[key]);sample_ticks(85);assert(counts[key][BSP_BTN_LONG]==1);
  physical(3300);sample_ticks(20);assert(counts[key][BSP_BTN_CLICK]==1); // Long press is not a click.
  physical(voltages[key]);sample_ticks(4);physical(3300);sample_ticks(3);
  physical(voltages[key]);sample_ticks(4);physical(3300);sample_ticks(20);
  assert(counts[key][BSP_BTN_DOUBLE]==1&&counts[key][BSP_BTN_CLICK]==1);
 }
 for(int cycle=0;cycle<100;cycle++) {
  physical(595);sample_ticks(4);physical(3300);sample_ticks(20);
  if(!fallback)assert(!adc_live&&!timer->running);
 }
 if(!fallback) {
  assert(bsp_button_read_mv()==3300&&!adc_live&&!timer->running&&irq_enabled);
  fail_kind=4;assert(bsp_button_read_mv()==-1&&!adc_live&&irq_enabled);
  int before=counts[0][BSP_BTN_PRESS];physical(300);fail_kind=4;sample_ticks(5);
  assert(counts[0][BSP_BTN_PRESS]==before);physical(3300);sample_ticks(20);
  for(int fault=7;fault<=9;fault++) {
   physical(595);sample_ticks(4);physical(3300);fail_kind=fault;sample_ticks(20);
   assert(!timer->running&&!adc_live&&irq_enabled); // Wake-arm failure recovers without losing keys.
  }
  physical(0);sample_ticks(4);physical(3300);sample_ticks(20);
  assert(irq_count>0&&wake_enabled&&irq_enabled&&!adc_live);
 }
 printf("GPIO ladder wake/idle, real gesture state machine, ADC failures and fallback mode %d: PASS\n",mode);
}
'''
        with tempfile.TemporaryDirectory(prefix='pdkpass-button-') as folder:
            directory = Path(folder)
            (directory/'common.h').write_text(common)
            (directory/'component.c').write_text('#include "common.h"\n'+without_includes(component/'iot_button.c'))
            (directory/'bsp.c').write_text('#include "common.h"\n'+without_includes(ROOT/'components/bsp/src/bsp_button.c'))
            (directory/'test.c').write_text(fake)
            binary = directory/'test'
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-Wno-unused-variable','-Wno-unused-parameter','-Wno-sign-compare','-Wno-pointer-to-int-cast','-I'+folder,
                            str(directory/'component.c'),str(directory/'bsp.c'),str(directory/'test.c'),
                            '-o',str(binary)],check=True)
            for mode in [0,1,2,5,10,11,12]:
                subprocess.run([str(binary),str(mode)],check=True)

if __name__ == '__main__':
    unittest.main()
