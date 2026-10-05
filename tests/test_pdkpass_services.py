"""Execute production service functions with deterministic transport/RTOS fakes.

No ESP-IDF download is needed for this host gate. Function bodies and cache
layouts come directly from the production C files, not a copy of their logic.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def production_source(path):
    source = path.read_text()
    return re.sub(r'^#include "([^"\n]+\.inc)"$',
                  lambda match: production_source(path.parent / match[1]), source, flags=re.M)

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end] + '\n'

def compile_run(code, extra=()):
    with tempfile.TemporaryDirectory(prefix='pdkpass-services-') as directory:
        source = Path(directory) / 'test.c'
        binary = Path(directory) / 'test'
        # Service tests fake persistence at the cache API. The cache adapter's
        # partition access and initialization are tested separately with its real
        # production bodies and independent default/private NVS stores.
        adapters = {
            'pdkpass_cache_read_blob': (
                'static int pdkpass_cache_read_blob(const char *, const char *, void *, size_t *);',
                'static int pdkpass_cache_read_blob(const char *ns,const char *key,void *data,size_t *size) {nvs_handle_t h;int err=nvs_open(ns,NVS_READONLY,&h);if(err==ESP_OK){err=nvs_get_blob(h,key,data,size);nvs_close(h);}return err;}'),
            'pdkpass_cache_write_blob': (
                'static int pdkpass_cache_write_blob(const char *, const char *, const void *, size_t);',
                'static int pdkpass_cache_write_blob(const char *ns,const char *key,const void *data,size_t size) {nvs_handle_t h;int err=nvs_open(ns,NVS_READWRITE,&h);if(err==ESP_OK){err=nvs_set_blob(h,key,data,size);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}return err;}'),
            'pdkpass_cache_forget_namespace': (
                'static void pdkpass_cache_forget_namespace(const char *);',
                'static void pdkpass_cache_forget_namespace(const char *ns) {nvs_handle_t h;if(nvs_open(ns,NVS_READWRITE,&h)==ESP_OK){if(nvs_erase_all(h)==ESP_OK)nvs_commit(h);nvs_close(h);}}'),
        }
        declarations, definitions = [], []
        for name, (declaration, definition) in adapters.items():
            if name + '(' in code and not re.search(r'(?:esp_err_t|void) ' + name + r'\([^;]*\)\s*\{', code):
                declarations.append(declaration)
                definitions.append(definition)
        if declarations:
            code = '#include <stddef.h>\n' + '\n'.join(declarations) + '\n' + code + '\n' + '\n'.join(definitions)
        source.write_text(code)
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                   '-I' + str(ROOT / 'main'),
                   '-I' + str(ROOT / 'tools/pdkpass-simulator/stubs'),
                   str(source), *[str(ROOT / x) for x in extra], '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)

PRELUDE = r'''
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pdkpass_season.h"
#include "pdkpass_results_core.h"
#include "pdkpass_season_core.h"
#include "pdkpass_tracks.h"
#include "pdkpass_calendar.h"
#include "pdkpass_sync_policy.h"
static bool sync_held[PDKPASS_SYNC_COUNT];
void pdkpass_sync_hold(pdkpass_sync_service_t service, bool held) {sync_held[service]=held;}
static void pdkpass_http_release(void) {}
#define pdTRUE 1
#define ESP_FAIL -1
#define PDKPASS_PODIUM_SIZE 3
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef uint32_t TickType_t;
static int s_lock;
static int xSemaphoreTake(int lock, unsigned delay) {(void)lock; (void)delay; return 1;}
static void xSemaphoreGive(int lock) {(void)lock;}
static pdkpass_race_t races[24];
static size_t race_count = 2;
size_t pdkpass_season_race_count(void) {return race_count;}
unsigned pdkpass_season_year(void) {return 2026;}
bool pdkpass_season_race_get(size_t i, pdkpass_race_t *race) {
 if (i >= race_count) return false; *race = races[i]; return true;
}
'''

class Services(unittest.TestCase):

    def test_manual_ui_refreshes_completion_after_dark_pause(self):
        source = production_source(ROOT / 'main/pdkpass_ui.c')
        code = PRELUDE + r'''
#include "pdkpass_model.h"
typedef int lv_timer_t;
static pdkpass_state_t s_state={.page=PDKPASS_PAGE_RESULTS,.selected_race=1,.selected_session=PDKPASS_SESSION_FP1};
static unsigned s_idle_stage,s_results_sync_generation;
static uint32_t s_points_sync_generation[PDKPASS_POINTS_TARGET_COUNT];
static uint32_t now,s_results_notice_until,s_sync_reject_until;
static uint32_t s_points_notice_until[PDKPASS_POINTS_TARGET_COUNT];
static pdkpass_page_t s_sync_reject_page;
static char s_sync_reject_hint[32],hint[32];
static unsigned reads,paused,renders;
static int64_t s_sync_cooldown_until_us;
static int64_t esp_timer_get_time(void) {return (int64_t)now * 1000;}
static pdkpass_manual_status_t result={.generation=1,.state=PDKPASS_MANUAL_RUNNING,.session=PDKPASS_SESSION_FP1};
static uint32_t lv_tick_get(void) {return now;}
static void lv_timer_pause(lv_timer_t *timer) {assert(timer);paused++;}
static void set_hint(const char *value) {snprintf(hint,sizeof(hint),"%s",value);}
static const char *detail_session_short_label(unsigned session) {assert(session==PDKPASS_SESSION_FP1);return "FP1";}
bool pdkpass_results_manual_status(size_t *race,pdkpass_manual_status_t *out) {reads++;*race=1;*out=result;return true;}
static pdkpass_manual_status_t points[PDKPASS_POINTS_TARGET_COUNT];
bool pdkpass_season_manual_status(pdkpass_points_target_t target,pdkpass_manual_status_t *out) {reads++;*out=points[target];return true;}
static void render(void) {renders++;}
'''
        for sig in ['static bool sync_notice_active(', 'static void update_manual_hint(',
                    'static void manual_sync_tick(']:
            code += function(source, sig)
        code += r'''
int main(void) {
 lv_timer_t timer=1;now=1;manual_sync_tick(&timer);
 assert(!strcmp(hint,"FP1 SYNCING...")&&s_results_sync_generation==1);
 s_idle_stage=2;unsigned before=reads;result.state=PDKPASS_MANUAL_UPDATED;result.generation++;
 now=90000;manual_sync_tick(&timer);assert(reads==before&&paused==1&&s_results_sync_generation==1);
 now+=60000;manual_sync_tick(&timer);assert(reads==before&&s_results_notice_until==0);
 s_idle_stage=0;manual_sync_tick(NULL);
 assert(s_results_sync_generation==2&&s_results_notice_until==now+3000);
 assert(!strcmp(hint,"FP1 UPDATED")&&renders==2);
 now+=3001;manual_sync_tick(&timer);assert(!strcmp(hint,"UP/DN OK:SYNC HOLD:BACK"));
 s_sync_reject_page=s_state.page;s_sync_cooldown_until_us=esp_timer_get_time()+57100000;
 update_manual_hint();assert(!strcmp(hint,"WAIT 58s TO SYNC"));
 now+=55000;update_manual_hint();assert(!strcmp(hint,"WAIT 3s TO SYNC"));
 now+=2100;update_manual_hint();assert(!strcmp(hint,"UP/DN OK:SYNC HOLD:BACK"));
 s_state.page=PDKPASS_PAGE_STANDINGS;
 points[PDKPASS_POINTS_DRIVERS]=(pdkpass_manual_status_t){.state=PDKPASS_MANUAL_RUNNING,.generation=1};
 manual_sync_tick(&timer);assert(!strcmp(hint,"POINTS SYNCING..."));
 s_state.page=PDKPASS_PAGE_TEAM_STANDINGS;
 update_manual_hint();assert(!strcmp(hint,"UP/DN OK:SYNC HOLD:HOME"));
 points[PDKPASS_POINTS_DRIVERS].state=PDKPASS_MANUAL_UPDATED;
 points[PDKPASS_POINTS_DRIVERS].generation++;
 manual_sync_tick(&timer);assert(!strcmp(hint,"UP/DN OK:SYNC HOLD:HOME"));
 s_state.page=PDKPASS_PAGE_STANDINGS;
 update_manual_hint();assert(!strcmp(hint,"POINTS UPDATED"));
 points[PDKPASS_POINTS_TEAMS]=(pdkpass_manual_status_t){.state=PDKPASS_MANUAL_RUNNING,.generation=1};
 manual_sync_tick(&timer);assert(!strcmp(hint,"POINTS UPDATED"));
 s_state.page=PDKPASS_PAGE_TEAM_STANDINGS;
 update_manual_hint();assert(!strcmp(hint,"POINTS SYNCING..."));
 now+=3001;s_state.page=PDKPASS_PAGE_STANDINGS;
 update_manual_hint();assert(!strcmp(hint,"UP/DN OK:SYNC HOLD:HOME"));
 puts("Manual UI: wake completion, notice expiry and independent points footers: PASS");
}
'''
        compile_run(code)

    def test_sync_dates_are_independent_persisted_and_commit_gated(self):
        source = production_source(ROOT / 'main/pdkpass_sync.c')
        code = PRELUDE + r'''
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
static int s_guard;
static int64_t s_last_success[PDKPASS_SYNC_STATUS_COUNT],disk[PDKPASS_SYNC_STATUS_COUNT],staged[PDKPASS_SYNC_STATUS_COUNT];
static const char *const s_status_keys[]={"calendar","results","drivers","teams"};
static unsigned writes,commits,callbacks;
static bool fail_set,fail_commit;
static void changed(void) {callbacks++;}
static void (*s_status_callback)(void);
static int key_index(const char *key) {
 for(unsigned i=0;i<PDKPASS_SYNC_STATUS_COUNT;i++)if(!strcmp(key,s_status_keys[i]))return i;
 assert(false);return -1;
}
static int nvs_open(const char *ns,int mode,int *handle) {assert(!strcmp(ns,"pdk_sync"));*handle=mode+1;return ESP_OK;}
static int nvs_get_i64(int h,const char *key,int64_t *out) {assert(h==1);*out=disk[key_index(key)];return ESP_OK;}
static int nvs_set_i64(int h,const char *key,int64_t value) {
 assert(h==2);writes++;if(fail_set)return ESP_FAIL;
 memcpy(staged,disk,sizeof(disk));staged[key_index(key)]=value;return ESP_OK;
}
static int nvs_commit(int h) {assert(h==2);commits++;if(fail_commit)return ESP_FAIL;memcpy(disk,staged,sizeof(disk));return ESP_OK;}
static void nvs_close(int h) {(void)h;}
'''
        for sig in ['void pdkpass_sync_status_init(', 'int64_t pdkpass_sync_last_success(',
                    'void pdkpass_sync_mark_success(']:
            code += function(source, sig)
        code += r'''
int main(void) {
 int64_t old=1767225600LL,next=old+86400;
 disk[0]=old;disk[1]=old;pdkpass_sync_status_init(changed);
 assert(pdkpass_sync_last_success(PDKPASS_SYNC_STATUS_CALENDAR)==old);
 assert(!pdkpass_sync_last_success(PDKPASS_SYNC_STATUS_DRIVERS));
 for(unsigned i=0;i<PDKPASS_SYNC_STATUS_COUNT;i++) {
  pdkpass_sync_mark_success((pdkpass_sync_status_t)i,next);
  pdkpass_sync_mark_success((pdkpass_sync_status_t)i,next+60);
 }
 assert(writes==4&&commits==4&&callbacks==4);
 fail_set=true;pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_DRIVERS,next+86400);
 assert(pdkpass_sync_last_success(PDKPASS_SYNC_STATUS_DRIVERS)==next&&callbacks==4);
 fail_set=false;fail_commit=true;pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_TEAMS,next+86400);
 assert(pdkpass_sync_last_success(PDKPASS_SYNC_STATUS_TEAMS)==next&&callbacks==4);
 fail_commit=false;pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_TEAMS,next+86400);
 assert(callbacks==5&&disk[3]==next+86400&&disk[0]==next);
 memset(s_last_success,0,sizeof(s_last_success));pdkpass_sync_status_init(changed);
 assert(pdkpass_sync_last_success(PDKPASS_SYNC_STATUS_TEAMS)==next+86400);
 assert(pdkpass_sync_last_success(PDKPASS_SYNC_STATUS_DRIVERS)==next);
 unsigned before=writes;pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_COUNT,next);
 pdkpass_sync_mark_success(PDKPASS_SYNC_STATUS_RESULTS,0);assert(writes==before);
 puts("Sync dates: separate calendar/results/drivers/teams, legacy keys, daily dedup and failed commits: PASS");
}
'''
        compile_run(code)

    def test_audio_init_rolls_back_each_stage_and_failed_open_retries(self):
        source = production_source(ROOT / 'components/bsp/src/bsp_audio.c')
        code = PRELUDE + r'''
#define ESP_LOGE(...) ((void)0)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define BSP_I2S_PORT 0
#define BSP_I2S_MCLK 6
#define BSP_I2S_BCLK 5
#define BSP_I2S_WS 3
#define BSP_I2S_DOUT 2
#define BSP_I2S_DIN 4
#define BSP_I2S_PA_CTRL -1
#define BSP_I2C_PORT 0
#define BSP_I2C_ES8311_ADDR 0x18
#define I2S_ROLE_MASTER 0
#define I2S_CLK_SRC_DEFAULT 0
#define I2S_MCLK_MULTIPLE_256 256
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_BIT_WIDTH_AUTO 0
#define I2S_SLOT_MODE_STEREO 2
#define I2S_STD_SLOT_BOTH 3
#define ESP_CODEC_DEV_WORK_MODE_DAC 2
#define ESP_CODEC_DEV_TYPE_OUT 2
#define ESP_CODEC_DEV_WORK_MODE_BOTH 3
#define ESP_CODEC_DEV_TYPE_IN_OUT 3
#define ESP_CODEC_DEV_MAKE_CHANNEL_MASK(x) (1U<<(x))
typedef struct {unsigned id,role,dma_desc_num,dma_frame_num,intr_priority;bool auto_clear_after_cb,auto_clear_before_cb;} i2s_chan_config_t;
typedef struct {
 struct {unsigned sample_rate_hz,clk_src,ext_clk_freq_hz,mclk_multiple;} clk_cfg;
 struct {unsigned data_bit_width,slot_bit_width,slot_mode,slot_mask,ws_width;bool ws_pol,bit_shift,left_align,big_endian,bit_order_lsb;} slot_cfg;
 struct {int mclk,bclk,ws,dout,din;struct {bool mclk_inv,bclk_inv,ws_inv;} invert_flags;} gpio_cfg;
} i2s_std_config_t;
typedef struct {unsigned state;} *i2s_chan_handle_t;
typedef struct {int id;} audio_codec_ctrl_if_t,audio_codec_data_if_t,audio_codec_gpio_if_t,audio_codec_if_t;
typedef struct {unsigned port,addr;void *bus_handle;} audio_codec_i2c_cfg_t;
typedef struct {unsigned port;i2s_chan_handle_t tx_handle,rx_handle;} audio_codec_i2s_cfg_t;
typedef struct {const audio_codec_ctrl_if_t *ctrl_if;const audio_codec_gpio_if_t *gpio_if;int codec_mode,pa_pin;bool pa_reverted,master_mode,use_mclk,no_dac_ref;struct {float pa_voltage,codec_dac_voltage;} hw_gain;} es8311_codec_cfg_t;
typedef struct {int dev_type;const audio_codec_if_t *codec_if;const audio_codec_data_if_t *data_if;} esp_codec_dev_cfg_t;
typedef struct {unsigned bits_per_sample,channel,channel_mask,sample_rate,mclk_multiple;} esp_codec_dev_sample_info_t;
typedef void *esp_codec_dev_handle_t;
static esp_codec_dev_handle_t s_dev;
static i2s_chan_handle_t s_tx,s_rx;
static uint32_t s_hz;static uint8_t s_bits,s_ch;
static bool s_playback_only;
static bool s_opened,s_reopen_after_stop,partial_open,fail_open;
static const char *TAG="test";
static unsigned stage,fail_stage,live,opens,closes;
static const void *ctrl,*data,*gpio,*codec;
static bool failed(void) {return ++stage==fail_stage;}
static void *new_object(void) {void *p=calloc(1,16);assert(p);live++;return p;}
static void delete_object(const void *p) {assert(p&&live);free((void *)p);live--;}
static int bsp_i2c_init(void) {return failed()?ESP_FAIL:ESP_OK;}
static void *bsp_i2c_bus(void) {return (void *)1;}
static const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *cfg) {
 assert(cfg->bus_handle==(void *)1&&cfg->addr==0x30);return failed()?NULL:(ctrl=new_object());
}
static int i2s_new_channel(const i2s_chan_config_t *cfg,i2s_chan_handle_t *tx,i2s_chan_handle_t *rx) {
 assert(cfg->dma_desc_num==6&&cfg->dma_frame_num==240&&!s_tx&&!s_rx);
 if(failed())return ESP_FAIL;*tx=new_object();if(rx)*rx=new_object();return ESP_OK;
}
static int i2s_channel_init_std_mode(i2s_chan_handle_t h,const i2s_std_config_t *cfg) {
 assert(h&&!h->state&&cfg->clk_cfg.sample_rate_hz==16000);if(failed())return ESP_FAIL;h->state=1;return ESP_OK;
}
static int i2s_channel_enable(i2s_chan_handle_t h) {assert(h&&h->state==1);if(failed())return ESP_FAIL;h->state=2;return ESP_OK;}
static int i2s_channel_disable(i2s_chan_handle_t h) {assert(h&&h->state==2);h->state=1;return ESP_OK;}
static int i2s_del_channel(i2s_chan_handle_t h) {assert(h&&h->state!=2&&!data);delete_object(h);return ESP_OK;}
static const audio_codec_data_if_t *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *cfg) {
 assert(cfg->tx_handle==s_tx&&cfg->rx_handle==s_rx);return failed()?NULL:(data=new_object());
}
static const audio_codec_gpio_if_t *audio_codec_new_gpio(void) {return failed()?NULL:(gpio=new_object());}
static const audio_codec_if_t *es8311_codec_new(const es8311_codec_cfg_t *cfg) {
 assert(cfg->ctrl_if==ctrl&&cfg->gpio_if==gpio&&ctrl&&gpio&&cfg->no_dac_ref);assert(cfg->codec_mode==(s_playback_only?ESP_CODEC_DEV_WORK_MODE_DAC:ESP_CODEC_DEV_WORK_MODE_BOTH));return failed()?NULL:(codec=new_object());
}
static void *esp_codec_dev_new(const esp_codec_dev_cfg_t *cfg) {
 assert(cfg->codec_if==codec&&cfg->data_if==data&&codec&&data);assert(cfg->dev_type==(s_playback_only?ESP_CODEC_DEV_TYPE_OUT:ESP_CODEC_DEV_TYPE_IN_OUT));return failed()?NULL:new_object();
}
static int audio_codec_delete_codec_if(const audio_codec_if_t *p) {assert(ctrl&&gpio&&codec==p);delete_object(p);codec=NULL;return 0;}
static int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *p) {assert(!codec&&gpio==p);delete_object(p);gpio=NULL;return 0;}
static int audio_codec_delete_data_if(const audio_codec_data_if_t *p) {assert(!codec&&s_tx&&data==p);delete_object(p);data=NULL;return 0;}
static int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *p) {assert(!codec&&!data&&!s_tx&&!s_rx&&ctrl==p);delete_object(p);ctrl=NULL;return 0;}
static int esp_codec_dev_open(void *dev,const esp_codec_dev_sample_info_t *fs) {
 assert(dev==s_dev&&s_tx->state==2&&(!s_rx||s_rx->state==2)&&fs->sample_rate==16000);partial_open=true;opens++;return fail_open?ESP_FAIL:ESP_OK;
}
static int esp_codec_dev_close(void *dev) {
 assert(dev==s_dev&&partial_open);partial_open=false;closes++;i2s_channel_disable(s_tx);if(s_rx)i2s_channel_disable(s_rx);return ESP_OK;
}
static void esp_codec_dev_set_in_gain(void *dev,float gain) {assert(dev==s_dev&&gain==30);}
'''
        for sig in ['static void delete_i2s_channel(', 'static esp_err_t i2s_init(',
                    'static esp_err_t audio_init_mode(', 'esp_err_t bsp_audio_init(', 'esp_err_t bsp_audio_init_playback(', 'esp_err_t bsp_audio_set_format(']:
            code += function(source, sig)
        code += r'''
int main(void) {
 for(unsigned i=1;i<=11;i++) {
  stage=0;fail_stage=i;
  assert(bsp_audio_init()!=ESP_OK&&stage==i);
  assert(!s_dev&&!s_tx&&!s_rx&&!ctrl&&!data&&!gpio&&!codec&&!live);
  stage=0;fail_stage=0;assert(bsp_audio_init()==ESP_OK&&stage==11&&live==7);
  unsigned before=stage;assert(bsp_audio_init()==ESP_OK&&stage==before);
  // Test-only teardown of a successful allocation, keeping BSP production init idempotent.
  delete_object(s_dev);s_dev=NULL;
  audio_codec_delete_codec_if(codec);audio_codec_delete_gpio_if(gpio);audio_codec_delete_data_if(data);
  delete_i2s_channel(&s_rx,true);delete_i2s_channel(&s_tx,true);audio_codec_delete_ctrl_if(ctrl);
 }
 for(unsigned i=1;i<=9;i++) {
  stage=0;fail_stage=i;assert(bsp_audio_init_playback()!=ESP_OK&&stage==i);
  assert(!s_dev&&!s_tx&&!s_rx&&!live);
  stage=0;fail_stage=0;assert(bsp_audio_init_playback()==ESP_OK&&stage==9&&live==6&&!s_rx);
  assert(bsp_audio_init()==ESP_ERR_INVALID_STATE);
  delete_object(s_dev);s_dev=NULL;
  audio_codec_delete_codec_if(codec);audio_codec_delete_gpio_if(gpio);audio_codec_delete_data_if(data);
  delete_i2s_channel(&s_tx,true);audio_codec_delete_ctrl_if(ctrl);
 }
 stage=0;assert(bsp_audio_init()==ESP_OK);
 fail_open=true;assert(bsp_audio_set_format(16000,16,1)==ESP_FAIL);
 assert(!s_opened&&s_reopen_after_stop&&!partial_open&&s_tx->state==1&&s_rx->state==1&&closes==1);
 fail_open=false;assert(bsp_audio_set_format(16000,16,1)==ESP_OK);
 assert(s_opened&&!s_reopen_after_stop&&opens==2);
 assert(bsp_audio_set_format(16000,16,1)==ESP_OK&&opens==2);
 puts("Audio ownership: all 11 startup failures roll back, immediate init retry, partial-open close and reopen: PASS");
}
'''
        compile_run(code)

    def test_failed_manual_cache_save_retries_offline_without_network_and_survives_reload(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code = PRELUDE + defines + '\n' + types + r"""
#include <setjmp.h>
#include "pdkpass_results.h"
#include "pdkpass_network.h"
#include "pdkpass_sync_policy.h"
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define pdFALSE 0
#define EVENT_WAKE 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
typedef int nvs_handle_t;
static const char *TAG="test",*NVS_NAMESPACE="test",*NVS_KEY="season";
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static unsigned s_cache_year;
static size_t s_cache_count,s_requested_race=SIZE_MAX,s_priority_race=SIZE_MAX;
static int64_t s_priority_until_utc,s_cache_retry_at_us;
static bool s_cache_dirty,s_has_cached_data,s_online=true,s_force_pending=true;
static size_t s_force_race;
static pdkpass_manual_status_t s_force_status={.state=PDKPASS_MANUAL_RUNNING,
 .generation=1,.session=PDKPASS_SESSION_FP1,.deadline_us=120000000};
static int s_events=1;
static results_store_t staging,persisted;
static unsigned commits,sets,discoveries,fetches,cues,plans,try_calls,ends;
static unsigned forced_failure_stage;
static int64_t now_us=1000,network_due_ms;
static time_t wall_utc=1788688800;
static bool held,available=true,replace_on_try;
void pdkpass_sync_mark_success(pdkpass_sync_status_t status,int64_t utc) {(void)status;(void)utc;assert(false);}
static int64_t esp_timer_get_time(void) {return now_us;}
static time_t fake_time(time_t *out) {(void)out;return wall_utc;}
#define time fake_time
static void load_cache(void);
static bool pdkpass_http_try_begin(void) {
 try_calls++;if(!available)return false;
 assert(!held);held=true;
 if(replace_on_try){replace_on_try=false;load_cache();}
 return true;
}
static bool pdkpass_http_begin_until(int64_t deadline) {assert(deadline>now_us&&!held);held=true;return true;}
static void pdkpass_http_begin(void) {assert(false);}
static bool pdkpass_http_expired(void) {return false;}
static void pdkpass_http_end(void) {assert(held);held=false;ends++;}
static int nvs_open(const char *name,int mode,int *handle) {
 (void)name;*handle=mode+1;
 if(mode==NVS_READWRITE&&forced_failure_stage==1)return ESP_FAIL;
 return ESP_OK;
}
static int nvs_set_blob(int handle,const char *key,const void *data,size_t size) {
 assert(held&&handle==2&&size==sizeof(staging));(void)key;sets++;
 if(forced_failure_stage==2)return ESP_FAIL;
 memcpy(&staging,data,size);return ESP_OK;
}
static int nvs_commit(int handle) {
 assert(held&&handle==2);commits++;
 if(commits<=2||forced_failure_stage==3)return ESP_FAIL;
 persisted=staging;return ESP_OK;
}
static int nvs_get_blob(int handle,const char *key,void *data,size_t *size) {
 (void)key;assert(handle==1&&*size>=sizeof(persisted));
 memcpy(data,&persisted,sizeof(persisted));*size=sizeof(persisted);return ESP_OK;
}
static void nvs_close(int handle) {(void)handle;}
static int nvs_erase_all(int handle) {(void)handle;assert(false);return ESP_FAIL;}
static bool discover_sessions(size_t race,race_cache_t *cache,int64_t now) {
 assert(race==0&&held);(void)now;discoveries++;cache->discovered=1;return true;
}
static bool fetch_result(session_cache_t *session) {
 assert(held&&session->session_key==100);fetches++;
 strcpy(session->podium_codes[0],"NEW");session->ready=1;return true;
}
static void callback(size_t race,bool fresh) {assert(race==0);if(fresh)cues++;}
static pdkpass_results_callback_t s_callback=callback;
static bool request_pending(void) {return false;}
static bool online_snapshot(void) {return s_online;}
uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t service) {
 assert(service==PDKPASS_SYNC_RESULTS);
 return network_due_ms>now_us/1000 ? (uint32_t)(network_due_ms-now_us/1000) : 0;
}
void pdkpass_sync_plan(pdkpass_sync_service_t service,uint32_t ms) {
 assert(service==PDKPASS_SYNC_RESULTS&&ms==RESULTS_IDLE_DELAY_MS);
 plans++;network_due_ms=now_us/1000+ms;
}
esp_err_t pdkpass_network_request(pdkpass_network_command_t command) {(void)command;assert(false);return ESP_OK;}
static size_t select_race(int64_t now) {(void)now;assert(false);return SIZE_MAX;}
static void process_race(size_t race,int64_t now) {(void)race;(void)now;assert(false);}
static void vTaskDelay(unsigned ticks) {(void)ticks;}
static jmp_buf done;
static unsigned waits;
static void xEventGroupWaitBits(int e,int bits,int clear,int all,unsigned delay) {
 (void)e;(void)bits;(void)clear;(void)all;
 switch(waits++) {
 case 0:assert(delay==RESULTS_IDLE_DELAY_MS);break; // user starts manual refresh
 case 1:assert(delay==60000);now_us=30001000;s_online=false;wall_utc-=86400;break;
 case 2:assert(delay==30000);now_us=60001000;break; // still offline; second commit fails
 case 3:assert(delay==60000);now_us=120001000;available=false;break;
 case 4:assert(delay==1000);now_us=121001000;available=true;wall_utc+=7*86400;break;
 default:
  assert(delay>60000&&delay==(unsigned)(network_due_ms-now_us/1000));
  assert(!s_cache_dirty&&!s_cache_retry_at_us&&!held);longjmp(done,1);
 }
}
"""
        for signature in ['static void reset_race_cache(', 'static bool persisted_matches(',
                          'static int stored_index(', 'static void load_cache(',
                          'static esp_err_t save_cache(', 'static esp_err_t save_cache_with_retry(',
                          'static uint32_t cache_retry_wait_ms(', 'static TickType_t cache_retry_wait_ticks(',
                          'static void retry_cache_if_due(', 'static void finish_manual_race(',
                          'static bool take_manual_race(', 'static bool manual_race_pending(',
                          'static int64_t manual_deadline(', 'static bool persisted_race_changed(',
                          'static void process_manual_race(', 'static int64_t retry_interval_seconds(',
                          'static bool cache_complete(', 'static TickType_t next_scheduled_wait(',
                          'static void results_task(']:
            code += function(source, signature)
        code += r"""
int main(void) {
 s_lock=1;
 for(size_t i=0;i<2;i++) {
  races[i].meeting_key=(int)(10+i);races[i].switch_at_utc=wall_utc-10*86400;
  s_cache[i].meeting_key=races[i].meeting_key;s_cache[i].discovered=1;
  for(size_t session=0;session<PDKPASS_SESSION_COUNT;session++) {
   session_cache_t *sample=&s_cache[i].sessions[session];
   sample->present=sample->ready=1;sample->session_key=(int)(100+i*10+session);
   sample->end_utc=wall_utc-4000;strcpy(sample->podium_codes[0],"OLD");
  }
 }
 if(setjmp(done)==0)results_task(NULL);
 assert(commits==3&&sets==3&&try_calls==3&&ends==3&&plans==1);
 assert(fetches==1&&discoveries==1&&cues==0&&s_has_cached_data);
 assert(s_force_status.state==PDKPASS_MANUAL_FAILED); // first attempt reported its actual save failure
 assert(strcmp(persisted.races[0].podium_codes[0][0],"NEW")==0);
 assert(strcmp(persisted.races[0].podium_codes[1][0],"OLD")==0);
 // Reboot/season replacement reloads the committed snapshot and clears old retry timing.
 memset(s_cache,0,sizeof(s_cache));s_cache_dirty=true;s_cache_retry_at_us=1;s_has_cached_data=false;
 load_cache();
 assert(s_cache[0].sessions[0].ready&&strcmp(s_cache[0].sessions[0].podium_codes[0],"NEW")==0);
 assert(!s_cache_dirty&&!s_cache_retry_at_us&&s_has_cached_data);
 // Open/set/commit errors all preserve dirty state and get their own retry deadline.
 for(unsigned stage=1;stage<=3;stage++) {
  forced_failure_stage=stage;held=true;s_cache_dirty=true;
  assert(save_cache_with_retry()==ESP_FAIL);held=false;
  assert(s_cache_dirty&&s_cache_retry_at_us==now_us+60000000);
  assert(cache_retry_wait_ms()==60000);
 }
 forced_failure_stage=0;
 // Ownership changed before retry acquired the shared transaction: never save stale data.
 s_cache_retry_at_us=now_us;replace_on_try=true;unsigned before=commits;
 retry_cache_if_due();assert(commits==before&&!s_cache_dirty&&!s_cache_retry_at_us&&!held);
 puts("Failed manual NVS save: offline 60s retry, busy ownership, RTC jumps, reload and stage failures: PASS");
}
"""
        compile_run(code)

    def test_manual_workers_use_bounded_http_and_finish_without_unowned_unlock(self):
        for service in ('results', 'season'):
            source = production_source(ROOT / f'main/pdkpass_{service}.c')
            code = PRELUDE + r"""
#include <setjmp.h>
static int64_t esp_timer_get_time(void) {return 0;}
#include "pdkpass_network.h"
#include "pdkpass_sync_policy.h"
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define EVENT_WAKE 1
#define RESULTS_IDLE_DELAY_MS 86400000U
#define RESULTS_BACKFILL_DELAY_MS 5000U
#define SEASON_MIN_REPEAT_SECONDS 300
static jmp_buf done;
static int s_events=1;
static bool pending=true,online=true,acquire,disconnect_on_acquire,s_cache_dirty;
static unsigned waits,begins,ends,finishes;
static int64_t s_last_attempt_utc;
static pdkpass_manual_state_t terminal;
static void xEventGroupWaitBits(int e,int b,int clear,int all,unsigned wait) {
 (void)e;(void)b;(void)clear;(void)all;(void)wait;
 if(waits++)longjmp(done,1);
}
static bool online_snapshot(void) {return online;}
static bool network_ready(void) {return online;}
static bool manual_race_pending(void) {return pending;}
static bool points_force_pending(void) {return pending;}
static bool take_manual_race(size_t *race) {bool take=pending;pending=false;*race=0;return take;}
static bool take_points_force(void) {bool take=pending;pending=false;return take;}
static int64_t manual_deadline(void) {return 42;}
static bool pdkpass_http_begin_until(int64_t deadline) {
 assert(deadline==42);begins++;
 if(disconnect_on_acquire)online=false;
 return acquire;
}
static void pdkpass_http_begin(void) {assert(false);}
static void pdkpass_http_end(void) {assert(acquire);ends++;}
static bool pdkpass_http_expired(void) {return false;}
static void finish_manual_race(size_t race,pdkpass_manual_state_t state) {
 assert(race==0);finishes++;terminal=state;
}
static void finish_points_force(pdkpass_manual_state_t state) {finishes++;terminal=state;}
static void process_manual_race(size_t race,int64_t now) {(void)race;(void)now;assert(false);}
static pdkpass_manual_state_t synchronize_manual_points(int64_t now) {(void)now;assert(false);return PDKPASS_MANUAL_FAILED;}
static bool request_pending(void) {return false;}
static int save_cache(void) {assert(false);return ESP_OK;}
static int save_cache_with_retry(void) {return save_cache();}
static TickType_t cache_retry_wait_ticks(TickType_t delay) {return delay;}
static void retry_cache_if_due(void) {}
static TickType_t next_scheduled_wait(int64_t now) {(void)now;return 60000;}
static size_t select_race(int64_t now) {(void)now;assert(false);return 0;}
static void process_race(size_t race,int64_t now) {(void)race;(void)now;assert(false);}
static void refresh_builtin_year(int64_t now) {(void)now;}
static TickType_t offline_wait(uint32_t wait,int64_t now) {(void)now;return wait;}
static bool synchronize(int64_t now) {(void)now;assert(false);return false;}
static int64_t next_sync_deadline(int64_t now) {return now+300;}
uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t svc) {(void)svc;return 10;}
void pdkpass_sync_plan(pdkpass_sync_service_t svc,uint32_t ms) {(void)svc;(void)ms;}
void pdkpass_sync_mark_success(pdkpass_sync_status_t svc,int64_t utc) {(void)svc;(void)utc;assert(false);}
esp_err_t pdkpass_network_request(pdkpass_network_command_t command) {(void)command;assert(false);return ESP_OK;}
"""
            code += function(source, f'static void {service}_task(')
            code += f"""
int main(void) {{
 if(setjmp(done)==0){service}_task(NULL);
 assert(begins==1&&ends==0&&finishes==1&&!pending&&terminal==PDKPASS_MANUAL_TIMED_OUT);
 waits=begins=ends=finishes=0;pending=online=acquire=disconnect_on_acquire=true;
 if(setjmp(done)==0){service}_task(NULL);
 assert(begins==1&&ends==1&&finishes==1&&!pending&&terminal==PDKPASS_MANUAL_OFFLINE);
 puts("Manual {service} worker: bounded lock, timeout, disconnect and owned cleanup: PASS");
}}
"""
            compile_run(code)

    def test_network_partial_initialization_retry_reuses_successful_stages(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r"""
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_LOGE(...) ((void)0)
#define PDKPASS_NETWORK_STARTING 0
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID 0
#define IP_EVENT_STA_GOT_IP 1
#define WIFI_STORAGE_RAM 1
#define WIFI_PS_MIN_MODEM 1
#define WIFI_INIT_CONFIG_DEFAULT() 0
typedef int wifi_init_config_t;
static int sta,ap;
static void *s_sta_netif,*s_ap_netif,*s_wifi_handler,*s_ip_handler;
static bool s_wifi_initialized,s_have_working_credentials;
static bool fail_ip_registration,fail_wifi_init;
static unsigned sta_creates,ap_creates,wifi_inits,wifi_regs,ip_regs,scans;
static int nvs_flash_init(void) {return ESP_OK;}
static void restore_last_time(void) {}
static int fake_setenv(const char *name,const char *value,int overwrite) {
 (void)name;(void)value;(void)overwrite;return 0;
}
#define setenv fake_setenv
static void fake_tzset(void) {}
#define tzset fake_tzset
static void publish_state(int state) {assert(state==PDKPASS_NETWORK_STARTING);}
static int esp_netif_init(void) {return ESP_OK;}
static int esp_event_loop_create_default(void) {return ESP_ERR_INVALID_STATE;}
static void *esp_netif_create_default_wifi_sta(void) {sta_creates++;return &sta;}
static void *esp_netif_create_default_wifi_ap(void) {ap_creates++;return &ap;}
static int configure_setup_address(void) {return ESP_OK;}
static int esp_wifi_init(const int *config) {(void)config;wifi_inits++;return fail_wifi_init?ESP_FAIL:ESP_OK;}
static void wifi_event(void) {}
static void ip_event(void) {}
static int esp_event_handler_instance_register(int base,int event,void (*fn)(void),
 void *arg,void **handle) {
 (void)event;(void)fn;(void)arg;
 if(base==WIFI_EVENT){wifi_regs++;*handle=&sta;}
 else {ip_regs++;if(fail_ip_registration)return ESP_FAIL;*handle=&ap;}
 return ESP_OK;
}
static int esp_wifi_set_storage(int storage) {(void)storage;return ESP_OK;}
static int esp_wifi_set_ps(int ps) {(void)ps;return ESP_OK;}
static bool load_credentials(void) {return true;}
static int scan_saved(void) {scans++;return ESP_OK;}
"""
        code += function(source, 'static esp_err_t prepare_network(')
        code += r"""
int main(void) {
 fail_ip_registration=true;
 assert(prepare_network()==ESP_FAIL);
 assert(sta_creates==1&&ap_creates==1&&wifi_inits==1&&wifi_regs==1&&ip_regs==1&&scans==0);
 fail_ip_registration=false;
 assert(prepare_network()==ESP_OK);
 assert(sta_creates==1&&ap_creates==1&&wifi_inits==1&&wifi_regs==1&&ip_regs==2&&scans==1);
 s_wifi_initialized=false;s_wifi_handler=s_ip_handler=NULL;fail_wifi_init=true;
 assert(prepare_network()==ESP_FAIL&&wifi_inits==2&&scans==1);
 fail_wifi_init=false;assert(prepare_network()==ESP_OK);
 assert(sta_creates==1&&ap_creates==1&&wifi_inits==3&&wifi_regs==2&&ip_regs==3&&scans==2);
 puts("Network retry reuses partial initialization without duplicate ownership: PASS");
}
"""
        compile_run(code)

    def test_ui_season_refresh_uses_persistent_storage_and_retries_busy_cache(self):
        source = production_source(ROOT / 'main/pdkpass_ui.c')
        code = PRELUDE + r"""
#include "pdkpass_model.h"
#include "pdkpass_schedule.h"
static pdkpass_season_snapshot_t s_season;
static pdkpass_team_snapshot_t s_team_standings;
static pdkpass_state_t s_state;
static int s_list_page;
static bool s_time_valid,season_ready,teams_ready;
static unsigned renders;
bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *out) {
 assert(out==&s_season);if(!season_ready)return false;
 out->race_count=2;out->driver_count=2;return true;
}
bool pdkpass_season_team_snapshot(pdkpass_team_snapshot_t *out) {
 assert(out==&s_team_standings);if(!teams_ready)return false;
 out->count=2;return true;
}
static void render(void) {renders++;}
size_t pdkpass_schedule_next_race(int64_t now,const pdkpass_race_t *races,size_t count) {
 (void)now;(void)races;(void)count;return 0;
}
void pdkpass_state_set_home_race(pdkpass_state_t *state,size_t race,size_t count) {
 (void)state;(void)race;(void)count;
}
"""
        code += function(source, 'bool pdkpass_ui_season_update(')
        code += r"""
int main(void) {
 s_state.selected_race=s_state.selected_driver=s_state.selected_team=23;
 assert(!pdkpass_ui_season_update()&&renders==0);
 season_ready=true;assert(!pdkpass_ui_season_update()&&renders==0);
 teams_ready=true;assert(pdkpass_ui_season_update()&&renders==1);
 assert(s_state.selected_race==1&&s_state.selected_driver==1&&s_state.selected_team==1);
 puts("UI direct snapshot storage, busy-cache retry and selection clamp: PASS");
}
"""
        compile_run(code)

    def test_network_allocation_failure_is_clean_and_user_retry_restarts(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "pdkpass_network.h"
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define NETWORK_TASK_STACK 4096
#define NETWORK_TASK_PRIORITY 4
#define pdPASS 1
#define EVENT_CANCEL 2048
#define EVENT_SYNC 8192
#define EVENT_POLICY 16384
#define EVENT_SETUP 1024
#define EVENT_RETRY 512
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef void *EventGroupHandle_t;
typedef unsigned EventBits_t;
static int s_start_lock, token;
static bool s_starting, fail_event, fail_mutex, fail_task;
static void *s_events, *s_candidate_lock;
static pdkpass_network_callback_t s_callback;
static unsigned event_deletes, mutex_deletes, creates, offline, queued;
static void *xEventGroupCreate(void) {return fail_event?NULL:&token;}
static void *xSemaphoreCreateMutex(void) {return fail_mutex?NULL:&token;}
static void vEventGroupDelete(void *p) {assert(p);event_deletes++;}
static void vSemaphoreDelete(void *p) {assert(p);mutex_deletes++;}
static void network_task(void *p) {(void)p;}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,
 void *arg,unsigned priority,void *handle) {
 (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;
 creates++;return !fail_task;
}
static void publish_state(pdkpass_network_state_t state) {assert(state==PDKPASS_NETWORK_OFFLINE);offline++;}
static void xEventGroupSetBits(void *p,unsigned bits) {assert(p);queued=bits;}
static void callback(const pdkpass_network_update_t *p) {(void)p;}
"""
        code += function(source, 'esp_err_t pdkpass_network_start(')
        code += function(source, 'esp_err_t pdkpass_network_request(')
        code += r"""
int main(void) {
 fail_task=true;
 assert(pdkpass_network_start(callback)==ESP_ERR_NO_MEM);
 assert(!s_events&&!s_candidate_lock&&!s_starting&&offline==0);
 assert(event_deletes==1&&mutex_deletes==1);
 assert(pdkpass_network_request(PDKPASS_NETWORK_SYNC)==ESP_ERR_INVALID_STATE);assert(creates==1);
 assert(pdkpass_network_request(PDKPASS_NETWORK_RETRY)==ESP_ERR_NO_MEM&&creates==2);
 assert(!s_events&&!s_candidate_lock&&offline==0);
 fail_task=false;assert(pdkpass_network_request(PDKPASS_NETWORK_OPEN_SETUP)==ESP_OK);
 assert(creates==3&&s_events&&s_candidate_lock&&queued==EVENT_SETUP);
 assert(pdkpass_network_start(callback)==ESP_ERR_INVALID_STATE);
 s_events=s_candidate_lock=NULL;fail_event=true;
 assert(pdkpass_network_start(callback)==ESP_ERR_NO_MEM);
 assert(event_deletes==2&&mutex_deletes==3&&!s_events&&!s_candidate_lock);
 fail_event=false;fail_mutex=true;
 assert(pdkpass_network_start(callback)==ESP_ERR_NO_MEM);
 assert(event_deletes==3&&mutex_deletes==3&&!s_events&&!s_candidate_lock);
 fail_mutex=false;pdkpass_network_request(PDKPASS_NETWORK_RETRY);
 assert(creates==4&&queued==EVENT_RETRY);
 puts("Network orphan cleanup and nonblocking user restart: PASS");
}
"""
        compile_run(code)

    def test_ui_updates_survive_lock_failure_coalesce_and_own_strings(self):
        source = production_source(ROOT / 'main/main.c')
        code = PRELUDE + r"""
#include "pdkpass_ui_updates.h"
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static int s_updates_lock;
static pdkpass_ui_updates_t s_updates;
static bool locked, lock_available, snapshot_available, update_during_render;
static unsigned networks, seasons, results, statuses, wakes;
static void wake_reminder_worker(void) {wakes++;}
static bool bsp_lvgl_lock(unsigned wait) {(void)wait;locked=lock_available;return locked;}
static void bsp_lvgl_unlock(void) {assert(locked);locked=false;}
static void on_data_status(void);
static void pdkpass_ui_network_update(const pdkpass_network_update_t *update) {
 assert(locked&&strcmp(update->setup_ssid,"Latest")==0);
 assert(strcmp(update->setup_password,"test-only")==0);
 assert(strcmp(update->setup_error,"ERROR")==0);networks++;
 if(update_during_render) {update_during_render=false;on_data_status();}
}
static bool pdkpass_ui_season_update(void) {assert(locked);if(!snapshot_available)return false;seasons++;return true;}
static void pdkpass_ui_results_update(size_t i) {assert(locked&&(i==0||i==23));results++;}
static void pdkpass_ui_sync_status_update(void) {assert(locked);statuses++;}
void pdkpass_season_set_network(bool online,bool valid) {(void)online;(void)valid;}
static void pdkpass_results_set_online(bool online) {(void)online;}
static void pdkpass_reminder_set_time_valid(bool valid) {(void)valid;}
static void pdkpass_results_season_changed(void) {}
static void pdkpass_reminder_season_changed(void) {}
static void pdkpass_sound_result_ready(void) {}
"""
        for signature in ['static void on_network(', 'static void on_season(',
                          'static void on_results(', 'static void on_data_status(',
                          'static bool ui_updates_pending(', 'static void dispatch_ui_updates(']:
            code += function(source, signature)
        code += r"""
int main(void) {
 char ssid[33]="First", password[65]="test-only", error[64]="ERROR";
 pdkpass_network_update_t update={.state=PDKPASS_NETWORK_SETUP,
  .setup_ssid=ssid,.setup_password=password,.setup_error=error};
 on_network(&update);strcpy(ssid,"Latest");on_network(&update);
 strcpy(ssid,"Changed");strcpy(password,"Changed");strcpy(error,"Changed");
 on_season();on_results(0,false);on_results(23,true);on_results(23,false);on_data_status();
 assert(wakes==7&&ui_updates_pending());
 dispatch_ui_updates(); // unavailable LVGL lock must not consume anything
 assert(ui_updates_pending()&&!networks&&!results&&!statuses);
 lock_available=true;update_during_render=true;
 dispatch_ui_updates(); // consume latest network; season snapshot is still busy
 assert(networks==1&&results==2&&statuses==1&&seasons==0);
 assert(s_updates.season&&s_updates.status); // newer producer survives take()
 snapshot_available=true;dispatch_ui_updates();
 assert(seasons==1&&statuses==2&&!ui_updates_pending());
 dispatch_ui_updates();assert(networks==1&&results==2&&statuses==2);
 puts("UI retained notifications, latest state, owned strings and snapshot retry: PASS");
}
"""
        compile_run(code, ['main/pdkpass_ui_updates.c'])

    def test_manual_deadline_terminal_status_and_ownership(self):
        for service in ('results', 'season'):
            source = production_source(ROOT / f'main/pdkpass_{service}.c')
            prefix = 's_force' if service == 'results' else 's_points_force'
            raw_status = prefix + '_status' + ('[PDKPASS_POINTS_DRIVERS]' if service == 'season' else '')
            code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include "pdkpass_results.h"
#include "pdkpass_season.h"
#include "pdkpass_sync_policy.h"
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define RESULTS_FORCE_COOLDOWN_MS 60000U
#define POINTS_FORCE_COOLDOWN_MS 60000U
#define EVENT_WAKE 1
typedef uint32_t TickType_t;
static int s_lock=1,s_events=1;
static bool s_online=false,s_time_valid=false,contention;
static bool radio_held;
void pdkpass_sync_hold(pdkpass_sync_service_t service,bool held) {(void)service;radio_held=held;}
static unsigned guaranteed_waits;
static int64_t now_us=10;
static int xSemaphoreTake(int lock,unsigned wait) {
 assert(lock==1);
 if(wait==portMAX_DELAY){guaranteed_waits++;return pdTRUE;}
 return !contention;
}
static void xSemaphoreGive(int lock) {assert(lock==1);}
static int64_t esp_timer_get_time(void) {return now_us;}
static TickType_t xTaskGetTickCount(void) {return (TickType_t)(now_us/1000);}
static void xEventGroupSetBits(int events,unsigned bits) {assert(events==1&&bits==1);}
static size_t s_force_race;
static bool s_force_pending,s_force_has_last_tick,s_points_force_pending;
static bool s_points_force_has_last_tick[PDKPASS_POINTS_TARGET_COUNT];
static pdkpass_points_target_t s_points_force_target;
static TickType_t s_force_last_tick,s_points_force_last_tick[PDKPASS_POINTS_TARGET_COUNT];
static pdkpass_manual_status_t s_force_status,s_points_force_status[PDKPASS_POINTS_TARGET_COUNT];
size_t pdkpass_season_race_count(void) {return 1;}
static bool race_is_eligible(size_t race,int64_t now) {(void)now;return race==0;}
"""
            if service == 'results':
                code += function(source, 'pdkpass_manual_state_t pdkpass_results_force_session(')
                code += function(source, 'bool pdkpass_results_manual_status(')
                code += function(source, 'static void finish_manual_race(')
                request = 'pdkpass_results_force_session(0, PDKPASS_SESSION_FP1)'
                getter = 'pdkpass_results_manual_status(&race, &status)'
                finish = 'finish_manual_race(0, PDKPASS_MANUAL_UPDATED)'
            else:
                code += function(source, 'pdkpass_manual_state_t pdkpass_season_force_points(')
                code += function(source, 'bool pdkpass_season_manual_status(')
                code += function(source, 'static void finish_points_force(')
                request = 'pdkpass_season_force_points(PDKPASS_POINTS_DRIVERS)'
                getter = 'pdkpass_season_manual_status(PDKPASS_POINTS_DRIVERS, &status)'
                finish = 'finish_points_force(PDKPASS_MANUAL_UPDATED)'
            points_checks = r"""
 assert(pdkpass_season_force_points(PDKPASS_POINTS_DRIVERS)==PDKPASS_MANUAL_COOLDOWN);
 assert(pdkpass_season_force_points(PDKPASS_POINTS_TEAMS)==PDKPASS_MANUAL_RUNNING);
 assert(s_points_force_target==PDKPASS_POINTS_TEAMS && radio_held);
 assert(pdkpass_season_manual_status(PDKPASS_POINTS_DRIVERS,&status)&&status.state==PDKPASS_MANUAL_UPDATED);
 assert(pdkpass_season_manual_status(PDKPASS_POINTS_TEAMS,&status)&&status.state==PDKPASS_MANUAL_RUNNING);
 assert(pdkpass_season_force_points(PDKPASS_POINTS_DRIVERS)==PDKPASS_MANUAL_BUSY);
 assert(s_points_force_target==PDKPASS_POINTS_TEAMS); // Cannot replace the accepted target.
 finish_points_force(PDKPASS_MANUAL_FAILED);
 assert(!radio_held);
 assert(pdkpass_season_force_points(PDKPASS_POINTS_TEAMS)==PDKPASS_MANUAL_COOLDOWN);
 assert(pdkpass_season_force_points(PDKPASS_POINTS_DRIVERS)==PDKPASS_MANUAL_COOLDOWN);
 assert(pdkpass_season_manual_status(PDKPASS_POINTS_DRIVERS,&status)&&status.state==PDKPASS_MANUAL_UPDATED);
 assert(pdkpass_season_manual_status(PDKPASS_POINTS_TEAMS,&status)&&status.state==PDKPASS_MANUAL_FAILED);
 assert(pdkpass_season_force_points(PDKPASS_POINTS_TARGET_COUNT)==PDKPASS_MANUAL_FAILED);
 assert(!pdkpass_season_manual_status((pdkpass_points_target_t)-1,&status));
""" if service == 'season' else ''
            code += f"""
int main(void) {{
 size_t race=0;pdkpass_manual_status_t status;
 assert({request}==PDKPASS_MANUAL_RUNNING);
 assert(radio_held);
 assert({raw_status}.deadline_us==10+PDKPASS_MANUAL_TIMEOUT_US);
 now_us={raw_status}.deadline_us-1;
 assert({getter}&&status.state==PDKPASS_MANUAL_RUNNING);
 now_us++;
 assert({getter}&&status.state==PDKPASS_MANUAL_TIMED_OUT&&status.generation==2);
 assert({raw_status}.state==PDKPASS_MANUAL_RUNNING); // worker still owns operation
 assert({request}==PDKPASS_MANUAL_BUSY); // cannot replace active operation
 contention=true;{finish};contention=false;
 assert(guaranteed_waits==1&&{raw_status}.state==PDKPASS_MANUAL_TIMED_OUT&&!radio_held);
 assert({getter}&&status.generation==2);
 assert({request}==PDKPASS_MANUAL_RUNNING); // cleanup finished, permit retry
 {finish};assert({raw_status}.state==PDKPASS_MANUAL_UPDATED&&!radio_held);
 {points_checks}
 puts("Manual deadline, ownership, independent cooldowns and terminal publication: PASS");
}}
"""
            compile_run(code)

    def test_http_transaction_deadline_acquisition_and_reset(self):
        source = production_source(ROOT / 'main/pdkpass_http.c')
        code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdio.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY UINT32_MAX
typedef uint32_t TickType_t;
static int s_transaction=1;
static bool s_transaction_active;
static void pdkpass_http_release(void) {}
static int64_t s_deadline_us,now_us;
static unsigned taken,given,last_wait;
static bool available;
static int64_t esp_timer_get_time(void) {return now_us;}
static int xSemaphoreTake(int lock,unsigned wait) {
 assert(lock==1);taken++;last_wait=wait;
 if(!available){now_us+=(int64_t)wait*1000;return 0;}
 return pdTRUE;
}
static void xSemaphoreGive(int lock) {assert(lock==1);given++;}
"""
        for signature in ['bool pdkpass_http_expired(', 'bool pdkpass_http_begin_until(',
                          'void pdkpass_http_begin(', 'bool pdkpass_http_try_begin(',
                          'void pdkpass_http_end(']:
            code += function(source, signature)
        code += r"""
int main(void) {
 now_us=1000000;
 assert(!pdkpass_http_begin_until(now_us)&&taken==0);
 assert(!pdkpass_http_begin_until(now_us+500000)&&last_wait==500);
 assert(now_us==1500000&&s_deadline_us==0&&given==0);
 available=true;
 assert(pdkpass_http_begin_until(now_us+1000000)&&s_deadline_us==2500000);
 assert(!pdkpass_http_expired());now_us=2500000;assert(pdkpass_http_expired());
 pdkpass_http_end();assert(given==1&&s_deadline_us==0&&!pdkpass_http_expired());
 assert(pdkpass_http_try_begin());pdkpass_http_end();
 pdkpass_http_begin();assert(last_wait==portMAX_DELAY);pdkpass_http_end();
 puts("HTTP bounded acquisition, deadline and transaction reset: PASS");
}
"""
        compile_run(code)

    def test_worker_start_failure_never_accepts_manual_sync(self):
        for service in ('results', 'season'):
            source = production_source(ROOT / f'main/pdkpass_{service}.c')
            defines = '\n'.join(x for x in source.splitlines()
                if x.startswith(('#define RESULTS_', '#define SEASON_', '#define POINTS_')))
            code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pdkpass_results.h"
#include "pdkpass_season.h"
#include "pdkpass_sync_policy.h"
#include <time.h>
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define pdPASS 1
#define pdTRUE 1
#define EVENT_WAKE 1
#define pdMS_TO_TICKS(x) (x)
typedef uint32_t TickType_t;
static int token;
static void *s_lock, *s_events;
static bool fail_mutex, fail_event, fail_task;
static unsigned deleted, reports, created, loads, wakes;
static void *xSemaphoreCreateMutex(void) {return fail_mutex ? NULL : &token;}
static void *xEventGroupCreate(void) {return fail_event ? NULL : &token;}
static void vEventGroupDelete(void *p) {assert(p);deleted++;}
static int pdkpass_http_init(void) {return ESP_OK;}
static void pdkpass_http_report_data_failure(const char *stage,int err,size_t bytes)
{assert(stage && err==ESP_ERR_NO_MEM);(void)bytes;reports++;}
static void load_cache(void) {loads++;}
static void load_team_cache(void) {loads++;}
static void results_task(void *p) {(void)p;}
static void season_task(void *p) {(void)p;}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,
 void *arg,unsigned priority,void *handle)
{(void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;
 created++;return !fail_task;}
static int xSemaphoreTake(void *p,unsigned wait) {(void)wait;assert(p);return 1;}
static void xSemaphoreGive(void *p) {assert(p);}
static TickType_t xTaskGetTickCount(void) {return 100;}
static int64_t esp_timer_get_time(void) {return 100000;}
static void xEventGroupSetBits(void *p,unsigned bits) {assert(p&&bits==1);wakes++;}
static bool s_online=true,s_time_valid=true;
void pdkpass_sync_hold(pdkpass_sync_service_t service,bool held) {(void)service;(void)held;}
static bool s_force_pending,s_force_has_last_tick,s_points_force_pending;
static bool s_points_force_has_last_tick[PDKPASS_POINTS_TARGET_COUNT];
static pdkpass_points_target_t s_points_force_target;
static TickType_t s_force_last_tick,s_points_force_last_tick[PDKPASS_POINTS_TARGET_COUNT];
static size_t s_force_race;
static pdkpass_manual_status_t s_force_status,s_points_force_status[PDKPASS_POINTS_TARGET_COUNT];
size_t pdkpass_season_race_count(void) {return 1;}
static bool race_is_eligible(size_t race,int64_t now) {(void)now;return race==0;}
""" + defines + '\n'
            if service == 'results':
                code += 'static pdkpass_results_callback_t s_callback;\n'
                force = 'pdkpass_results_force_session(0, PDKPASS_SESSION_FP1)'
                code += function(source, 'pdkpass_manual_state_t pdkpass_results_force_session(')
            else:
                code += 'static pdkpass_season_callback_t s_callback;\n'
                force = 'pdkpass_season_force_points(PDKPASS_POINTS_DRIVERS)'
                code += function(source, 'pdkpass_manual_state_t pdkpass_season_force_points(')
            code += function(source, f'esp_err_t pdkpass_{service}_start(')
            code += f"""
int main(void) {{
 fail_task=true;
 assert(pdkpass_{service}_start(NULL)==ESP_ERR_NO_MEM);
 assert(s_events==NULL && s_lock!=NULL && deleted==1 && reports==1 && loads>0);
 assert({force}!=PDKPASS_MANUAL_RUNNING && wakes==0);
 fail_task=false;
 assert(pdkpass_{service}_start(NULL)==ESP_OK);
 assert(created==2 && s_events!=NULL);
 assert({force}==PDKPASS_MANUAL_RUNNING && wakes==1);
 assert(pdkpass_{service}_start(NULL)==ESP_ERR_INVALID_STATE);
 s_events=NULL;s_lock=NULL;fail_mutex=true;
 assert(pdkpass_{service}_start(NULL)==ESP_ERR_NO_MEM);
 assert(s_events==NULL && deleted==2);
 assert({force}!=PDKPASS_MANUAL_RUNNING);
 fail_mutex=false;fail_event=true;
 assert(pdkpass_{service}_start(NULL)==ESP_ERR_NO_MEM);
 assert(s_events==NULL && reports==3);
 puts("Worker allocation failure, cache retention and retry: PASS");
}}
"""
            compile_run(code)


    def test_reminder_persistence_failure_and_calendar(self):
        source = production_source(ROOT / 'main/pdkpass_reminder.c')
        types = source[source.index('typedef struct {'):source.index('static SemaphoreHandle_t')]
        code = PRELUDE + '#include <stdatomic.h>\n#include "pdkpass_reminder.h"\n' + types + r'''
#define REMINDER_MAGIC 0x5044524DU
#define REMINDER_VERSION 1U
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_NO_MEM 0x101
typedef int nvs_handle_t;
static void (*s_wake)(void);
static atomic_bool s_time_valid, s_enabled=true, s_calendar_changed=true;
static bool s_dirty;
static int64_t s_retry_utc;
static reminder_store_t disk;
static bool disk_exists, fail_save, cached_calendar;
static unsigned writes, wakes;
static int xSemaphoreCreateMutex(void) {return 1;}
static void wake(void) {wakes++;}
static const char *esp_err_to_name(int err) {(void)err;return "FAIL";}
static int nvs_open(const char *name,int mode,int *handle) {
 assert(strcmp(name,"pdk_reminder")==0);(void)mode;*handle=1;return ESP_OK;
}
static int nvs_get_blob(int h,const char *key,void *data,size_t *size) {
 (void)h;assert(!strcmp(key,"schedule"));
 if(!disk_exists)return ESP_FAIL;
 assert(*size==sizeof(disk));memcpy(data,&disk,sizeof(disk));return ESP_OK;
}
static int nvs_set_blob(int h,const char *key,const void *data,size_t size) {
 (void)h;assert(!strcmp(key,"schedule")&&size==sizeof(disk));writes++;
 if(fail_save)return ESP_FAIL;
 memcpy(&disk,data,size);disk_exists=true;return ESP_OK;
}
static int nvs_commit(int h) {(void)h;return ESP_OK;}
static void nvs_close(int h) {(void)h;}
bool pdkpass_season_has_cached_data(void) {return cached_calendar;}
'''
        for sig in ['static esp_err_t persist(', 'esp_err_t pdkpass_reminder_init(',
                    'void pdkpass_reminder_set_time_valid(', 'void pdkpass_reminder_season_changed(',
                    'static void reconcile_calendar(', 'bool pdkpass_reminder_enabled(',
                    'void pdkpass_reminder_set_enabled(', 'void pdkpass_reminder_update_round(',
                    'bool pdkpass_reminder_poll(', 'uint32_t pdkpass_reminder_wait_ms(']:
            code += function(source, sig)
        code += r'''
int main(void) {
 const int64_t start=1790400000LL;
 pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT]={0},alert;
 entries[0]=(pdkpass_reminder_entry_t){.start_utc=start,.session_key=123};
 s_lock=0;assert(pdkpass_reminder_init(wake)==ESP_OK);
 assert(pdkpass_reminder_enabled());
 races[0].round=1;races[0].meeting_key=42;
 pdkpass_reminder_update_round(2026,1,42,entries);
 assert(writes==0&&wakes==1); // Publish never performs NVS on the HTTP/UI caller.
 assert(!pdkpass_reminder_poll(start-601,&alert));
 assert(writes==1&&disk_exists);
 pdkpass_reminder_set_time_valid(true);
 assert(pdkpass_reminder_wait_ms(start-601)==1000);
 fail_save=true;
 assert(!pdkpass_reminder_poll(start-600,&alert));
 assert(!(s_store.schedule.entries[0].flags&PDKPASS_REMINDER_FIRED));
 assert(pdkpass_reminder_wait_ms(start-600)==5000);
 unsigned previous=writes;
 assert(!pdkpass_reminder_poll(start-599,&alert)&&writes==previous);
 fail_save=false;
 assert(pdkpass_reminder_poll(start-595,&alert));
 assert(alert.session_key==123&&alert.round==1);
 assert(disk.schedule.entries[0].flags&PDKPASS_REMINDER_FIRED);
 assert(!pdkpass_reminder_poll(start-594,&alert));
 assert(!pdkpass_reminder_poll(start-600,&alert)); // Backwards time step.
 s_lock=0;memset(&s_store,0,sizeof(s_store));atomic_store(&s_time_valid,false);
 assert(pdkpass_reminder_init(wake)==ESP_OK);
 pdkpass_reminder_set_time_valid(true);
 assert(!pdkpass_reminder_poll(start-590,&alert)); // Power cycle deduplication.
 previous=writes;pdkpass_reminder_set_enabled(false);
 assert(!pdkpass_reminder_enabled()&&writes==previous);
 assert(!pdkpass_reminder_poll(start-580,&alert));
 assert(disk.schedule.enabled==0);
 s_lock=0;assert(pdkpass_reminder_init(wake)==ESP_OK);
 assert(!pdkpass_reminder_enabled());
 pdkpass_reminder_set_enabled(true);
 entries[0].session_key=124;
 pdkpass_reminder_update_round(2026,1,42,entries);
 // A confirmed calendar cancellation prevents an otherwise due alert.
 cached_calendar=true;
 for(size_t i=0;i<pdkpass_season_race_count();i++) {
  races[i].round=(uint8_t)(i+1);races[i].meeting_key=500+(int)i;
 }
 pdkpass_reminder_season_changed();
 assert(!pdkpass_reminder_poll(start-570,&alert));
 assert(s_store.schedule.entries[0].flags&PDKPASS_REMINDER_CANCELLED);
 puts("Reminder NVS failure/retry, persisted dismissal, toggle and cancelled meeting: PASS");
}
'''
        compile_run(code, ['main/pdkpass_reminder_core.c'])

    def test_reminder_audio_stream_and_dismiss_gesture(self):
        source = production_source(ROOT / 'main/pdkpass_sound.c')
        main_source = production_source(ROOT / 'main/main.c')
        pcm = production_source(ROOT / 'assets/music/session_reminder_pcm.inc')
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include "bsp_button.h"
#include "pdkpass_sound_core.h"
#define SOUND_VOLUME 50U
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
typedef void *QueueHandle_t;
static QueueHandle_t s_queue=(void *)1,s_keys=(void *)2;
typedef struct {bsp_btn_t button;bsp_btn_ev_t event;bool reschedule;} key_event_t;
static atomic_bool s_reminder_active,s_reminder_cancelled=true;
static int s_consumed_button=-1;
static unsigned volume,stops,writes,samples,delays,key_cues,ui_events,cancel_at;
static pdkpass_sound_kind_t last_kind;
bool pdkpass_sound_consume_key(bsp_btn_t button,bsp_btn_ev_t event);
static int xQueueOverwrite(QueueHandle_t queue,const void *value) {
 assert(queue==s_queue);last_kind=*(const pdkpass_sound_kind_t *)value;
 if(last_kind!=PDKPASS_SOUND_REMINDER)key_cues++;
 return pdTRUE;
}
static int xQueueSend(QueueHandle_t queue,const void *value,unsigned wait) {
 assert(queue==s_keys&&wait==0);
 const key_event_t *e=value;
 assert(e->event==BSP_BTN_PRESS||e->event==BSP_BTN_CLICK||e->event==BSP_BTN_LONG);
 ui_events++;return pdTRUE;
}
static void bsp_audio_set_volume(unsigned value) {volume=value;}
static void bsp_audio_stop(void) {stops++;}
static void vTaskDelay(unsigned ticks) {assert(ticks==100);delays++;}
static const int16_t s_reminder_pcm[]={
''' + pcm + r'''
};
static int bsp_audio_write(const void *pcm,size_t bytes) {
 assert(volume==80&&pcm==s_reminder_pcm+samples&&bytes<=512&&bytes%2==0);
 samples+=bytes/2;writes++;
 if(writes==cancel_at)assert(pdkpass_sound_consume_key(BSP_BTN_OK,BSP_BTN_PRESS));
 return ESP_OK;
}
'''
        for sig in ['void pdkpass_sound_reminder_play(', 'void pdkpass_sound_reminder_stop(',
                    'bool pdkpass_sound_consume_key(', 'static void play_reminder(',
                    'void pdkpass_sound_key(']:
            code += function(source, sig)
        code += function(main_source, 'static void on_key(')
        code += r'''
int main(void) {
 assert(sizeof(s_reminder_pcm)==96000);
 pdkpass_sound_reminder_play();
 assert(last_kind==PDKPASS_SOUND_REMINDER&&atomic_load(&s_reminder_active));
 play_reminder();assert(samples==48000&&stops==1&&delays==1&&volume==50);
 // Notice lasts 15 seconds even though the three-second audio has ended.
 on_key(BSP_BTN_DOWN,BSP_BTN_PRESS,NULL);
 assert(!atomic_load(&s_reminder_active)&&ui_events==1&&key_cues==0);
 on_key(BSP_BTN_DOWN,BSP_BTN_LONG,NULL);
 on_key(BSP_BTN_DOWN,BSP_BTN_CLICK,NULL);
 assert(ui_events==1&&key_cues==0);
 on_key(BSP_BTN_DOWN,BSP_BTN_PRESS,NULL);
 on_key(BSP_BTN_DOWN,BSP_BTN_CLICK,NULL);
 assert(ui_events==2&&key_cues==1); // The next physical gesture works normally.
 writes=samples=stops=delays=0;cancel_at=5;
 pdkpass_sound_reminder_play();play_reminder();
 assert(writes==5&&samples==1280&&stops==1&&delays==0&&volume==50);
 assert(pdkpass_sound_consume_key(BSP_BTN_OK,BSP_BTN_CLICK));
 pdkpass_sound_reminder_stop();
 assert(atomic_load(&s_reminder_cancelled));
 puts("Approved reminder PCM streaming, 80/50 volume and one-gesture dismissal: PASS");
}
'''
        compile_run(code)

    def test_sound_press_dispatch_and_worker_lifecycle(self):
        source = production_source(ROOT / 'main/pdkpass_sound.c')
        main_source = production_source(ROOT / 'main/main.c')
        code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdatomic.h>
#include "bsp_button.h"
#include "pdkpass_sound_core.h"
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY UINT32_MAX
#define ESP_LOGW(...) ((void)0)
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
static QueueHandle_t s_queue=(void *)1, s_keys=(void *)2;
static int16_t s_pcm[PDKPASS_SOUND_SAMPLES];
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_event_t;
static jmp_buf done;
static unsigned step, init_calls, opens, stops, writes, sounds, ui_keys, probes;
static unsigned volume, delays;
static int64_t now_us;
static int64_t esp_timer_get_time(void) {return now_us;}
static bool retry_case, open_fail;
static bool init_fail, write_fail, coalesced, alerts_enabled=true;
static bool queued_disabled_result, queued_enabled_result;
bool pdkpass_reminder_enabled(void) {return alerts_enabled;}
static atomic_bool s_reminder_active, s_reminder_cancelled;
static void pdkpass_sound_reminder_stop(void) {atomic_store(&s_reminder_active,false);atomic_store(&s_reminder_cancelled,true);}

static void play_reminder(void) {assert(false);}
static bool pdkpass_sound_consume_key(bsp_btn_t btn,bsp_btn_ev_t ev) {(void)btn;(void)ev;return false;}
static pdkpass_sound_kind_t last_kind, rendered[4];
static int bsp_audio_init_playback(void) {init_calls++;return init_fail ? -1 : ESP_OK;}
static int bsp_audio_set_format(unsigned hz,unsigned bits,unsigned channels) {
 assert(hz==16000 && bits==16 && channels==1);opens++;return open_fail ? -1 : ESP_OK;
}
static void bsp_audio_set_volume(unsigned value) {volume=value;}
static void bsp_audio_stop(void) {stops++;}
static int bsp_audio_write(const void *pcm,size_t bytes) {
 assert(pcm==s_pcm && bytes==PDKPASS_SOUND_SAMPLES*sizeof(int16_t));
 assert(volume==(rendered[writes]==PDKPASS_SOUND_RESULT_READY ? 70U : 50U));
 writes++;return write_fail ? -1 : ESP_OK;
}
static void vTaskDelay(unsigned ticks) {assert(ticks==70);delays++;}
size_t pdkpass_sound_render(pdkpass_sound_kind_t kind,int16_t *pcm,size_t capacity) {
 assert(pcm==s_pcm && capacity==PDKPASS_SOUND_SAMPLES && writes<4);
 rendered[writes]=kind;return PDKPASS_SOUND_SAMPLES;
}
static int xQueueOverwrite(QueueHandle_t queue,const void *value) {
 assert(queue==s_queue);last_kind=*(const pdkpass_sound_kind_t *)value;sounds++;return pdTRUE;
}
static int xQueueSend(QueueHandle_t queue,const void *value,unsigned wait) {
 assert(wait==0);
 if(queue==s_queue) {
  last_kind=*(const pdkpass_sound_kind_t *)value;sounds++;return pdTRUE;
 }
 assert(queue==s_keys);
 const key_event_t *key=value;
 assert(key->event==BSP_BTN_CLICK || key->event==BSP_BTN_LONG);
 ui_keys++;return pdTRUE;
}
static int xQueueReceive(QueueHandle_t queue,void *value,unsigned wait) {
 assert(queue==s_queue);
 if(queued_enabled_result) {
  if(wait==0) return 0;
  if(step++==0) {
   *(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_RESULT_READY;
   return pdTRUE;
  }
  assert(wait==250 && writes==1 && volume==50 && delays==1);
  longjmp(done,1);
 }
 if(queued_disabled_result) {
  if(wait==0) return 0;
  if(step++==0) {
   *(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_RESULT_READY;
   return pdTRUE;
  }
  assert(wait==250);longjmp(done,1);
 }
 if(wait==0) {
  probes++;
  if(!coalesced) {coalesced=true;*(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_DOWN;return pdTRUE;}
  return 0;
 }
 if(retry_case) {
  assert(wait==portMAX_DELAY || (step==6&&wait==250));
  *(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_UP;
  switch(step++) {
  case 0:now_us=59000000;*(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_REMINDER;
   atomic_store(&s_reminder_active,true);return pdTRUE;
  case 1:assert(!atomic_load(&s_reminder_active)&&init_calls==1);now_us=60000000;return pdTRUE;
  case 2:assert(init_calls==2);now_us=119000000;return pdTRUE;
  case 3:assert(init_calls==2);now_us=120000000;init_fail=false;open_fail=true;return pdTRUE;
  case 4:assert(init_calls==3&&opens==1);now_us=179000000;return pdTRUE;
  case 5:assert(opens==1);now_us=180000000;open_fail=false;return pdTRUE;
  default:assert(init_calls==3&&opens==2&&writes==1&&delays==1);longjmp(done,1);
  }
 }
 if(init_fail) {
  assert(wait==portMAX_DELAY && opens==0 && stops==0 && writes==0);
  if(step++==0) {*(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_UP;return pdTRUE;}
  longjmp(done,1);
 }
 if(write_fail) {
  if(step++==0) {assert(wait==250);*(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_UP;return pdTRUE;}
  assert(wait==portMAX_DELAY && stops==1 && writes==1 && delays==0);
  longjmp(done,1);
 }
 switch(step++) {
 case 0:
  assert(init_calls==1 && opens==1 && volume==50 && wait==250);
  *(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_UP;return pdTRUE;
 case 1:
  assert(opens==1 && stops==0 && writes==1 && rendered[0]==PDKPASS_SOUND_DOWN);
  assert(wait==250);*(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_OK;return pdTRUE;
 case 2:
  assert(opens==1 && writes==2 && wait==250);return 0;
 case 3:
  assert(stops==1 && wait==portMAX_DELAY);
  *(pdkpass_sound_kind_t *)value=PDKPASS_SOUND_UP;return pdTRUE;
 case 4:
  assert(opens==2 && writes==3 && wait==250);return 0;
 default:
  assert(stops==2 && wait==portMAX_DELAY && delays==3);longjmp(done,1);
 }
}
"""
        code += '\n'.join(line for line in source.splitlines()
                          if line.startswith(('#define SOUND_', '#define RESULT_VOLUME'))) + '\n'
        code += function(source, 'static bool open_sound(')
        code += function(source, 'static void sound_worker(')
        code += function(source, 'void pdkpass_sound_key(')
        code += function(source, 'void pdkpass_sound_result_ready(')
        code += function(main_source, 'static void on_key(')
        code += r"""
int main(void) {
 on_key(BSP_BTN_UP,BSP_BTN_PRESS,NULL);
 assert(sounds==1 && last_kind==PDKPASS_SOUND_UP && ui_keys==0 && init_calls==0);
 on_key(BSP_BTN_UP,BSP_BTN_CLICK,NULL);
 assert(sounds==1 && ui_keys==1);
 on_key(BSP_BTN_DOWN,BSP_BTN_PRESS,NULL);
 assert(sounds==2 && last_kind==PDKPASS_SOUND_DOWN);
 on_key(BSP_BTN_DOWN,BSP_BTN_DOUBLE,NULL);
 assert(sounds==2 && ui_keys==1);
 on_key(BSP_BTN_OK,BSP_BTN_PRESS,NULL);
 assert(sounds==3 && last_kind==PDKPASS_SOUND_OK);
 on_key(BSP_BTN_OK,BSP_BTN_LONG,NULL);
 assert(sounds==4 && last_kind==PDKPASS_SOUND_BACK && ui_keys==2);
 pdkpass_sound_result_ready();
 assert(sounds==5 && last_kind==PDKPASS_SOUND_RESULT_READY && ui_keys==2);
 atomic_store(&s_reminder_active,true);
 pdkpass_sound_result_ready();
 assert(sounds==5); // A result cue cannot replace an active reminder.
 atomic_store(&s_reminder_active,false);
 alerts_enabled=false;
 pdkpass_sound_result_ready();
 assert(sounds==5); // ALERTS OFF suppresses new result cues.
 on_key(BSP_BTN_UP,BSP_BTN_PRESS,NULL);
 assert(sounds==6 && last_kind==PDKPASS_SOUND_UP); // Buttons still sound.
 alerts_enabled=true;
 if(setjmp(done)==0) sound_worker(NULL);
 assert(probes==3 && volume==50);
 step=init_calls=opens=stops=writes=delays=0;coalesced=false;write_fail=true;
 if(setjmp(done)==0) sound_worker(NULL);
 step=init_calls=opens=stops=writes=delays=0;write_fail=false;init_fail=true;
 if(setjmp(done)==0) sound_worker(NULL);
 step=init_calls=opens=stops=writes=delays=0;init_fail=false;
 queued_disabled_result=true;alerts_enabled=false;
 if(setjmp(done)==0) sound_worker(NULL);
 assert(writes==0); // A queued result cue is dropped if ALERTS turns off.
 step=init_calls=opens=stops=writes=delays=0;queued_disabled_result=false;
 queued_enabled_result=true;alerts_enabled=true;
 if(setjmp(done)==0) sound_worker(NULL);
 assert(writes==1 && rendered[0]==PDKPASS_SOUND_RESULT_READY && volume==50);
 step=init_calls=opens=stops=writes=delays=0;queued_enabled_result=false;
 retry_case=true;init_fail=true;now_us=0;coalesced=true;
 if(setjmp(done)==0) sound_worker(NULL);
 assert(!atomic_load(&s_reminder_active));
 puts("Press dispatch, warm audio reuse, idle stop and audio failure cleanup: PASS");
}
"""
        compile_run(code)

    def test_http_failure_stages_and_backoff(self):
        source = production_source(ROOT / 'main/pdkpass_http.c')
        types = source[source.index('typedef struct {'):source.index('} response_t;') + len('} response_t;')]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "pdkpass_json_stream.h"
typedef int esp_err_t;
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(x) (x)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_HTTP_EAGAIN 6
#define ESP_ERR_HTTP_CONNECT 7
#define ESP_ERR_HTTP_WRITE_DATA 8
#define ESP_ERR_HTTP_FETCH_HEADER 9
#define ESP_ERR_HTTP_CONNECTION_CLOSED 10
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#define MBEDTLS_ERR_NET_CONN_RESET -0x0050
#define MBEDTLS_ERR_SSL_CONN_EOF -0x7280
#define ESP_ERR_INVALID_ARG 5
#define MALLOC_CAP_8BIT 1
typedef struct cJSON { int object; } cJSON;
typedef bool (*pdkpass_http_item_fn)(const cJSON *, void *);
static cJSON parsed = {.object=1};
static cJSON *cJSON_ParseWithOpts(const char *json,const char **end,bool strict) {
 (void)strict;
 if (json[0]!='{') return NULL;
 if (end) *end=json+strlen(json);
 return &parsed;
}
static bool cJSON_IsObject(const cJSON *item) {return item && item->object;}
static void cJSON_Delete(cJSON *item) {(void)item;}
typedef struct fake_client *esp_http_client_handle_t;
typedef struct esp_http_client_event esp_http_client_event_t;
struct esp_http_client_event {
 int event_id;
 const char *header_key, *header_value, *data;
 int data_len;
 void *user_data;
 esp_http_client_handle_t client;
};
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_EVENT_ON_DATA 2
typedef struct {
 const char *url;
 esp_err_t (*event_handler)(esp_http_client_event_t *);
 void *user_data, *crt_bundle_attach;
 int timeout_ms, buffer_size;
 const char *user_agent;
 bool is_async,keep_alive_enable;
} esp_http_client_config_t;
static void *esp_crt_bundle_attach;
struct fake_client {esp_http_client_config_t config;int status;};
static struct fake_client client;
static esp_http_client_handle_t s_client;
static unsigned s_client_origin;
static bool s_transaction_active;
static esp_err_t esp_http_client_set_url(esp_http_client_handle_t c,const char *url) {c->config.url=url;return ESP_OK;}
static esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t c,void *data) {c->config.user_data=data;return ESP_OK;}
static int init_calls, cleanup_calls, log_calls, memory_logs, status_code=200;
static int64_t now_us;
#define MALLOC_CAP_INTERNAL 2
static size_t heap_caps_get_free_size(unsigned caps) {assert(caps==3);return 50000;}
static size_t heap_caps_get_largest_free_block(unsigned caps) {assert(caps==3);return 25000;}
static bool init_fails, realloc_fails, async_pending, closed;
static int async_calls, failed_attempts, tls_error_code, tls_error_flags, reconnect_logs, recovery_logs;
static esp_err_t tls_error_result;
static bool fail_after_body, fail_after_header;
static int64_t advance_us, chunk_advance_us, failed_advance_us;
static int body_timeout_ms[2];
static esp_err_t transport_error;
static const char *retry_after, *chunks[2];
static int chunk_count;
static char last_log[256];
static char endpoint_log[192];
static void capture_log(const char *format,...) {
 va_list args;va_start(args,format);
 vsnprintf(last_log,sizeof(last_log),format,args);
 if(strstr(last_log,"GET memory ")) {memory_logs++;assert(strstr(last_log,"start_free=50000 start_largest=25000 failure_free=50000 failure_largest=25000"));}
 if(strstr(last_log,"GET recovered ")) {recovery_logs++;assert(!strstr(last_log,"private")&&!strstr(last_log,"secret"));}
 if(strstr(last_log,"GET reconnect ")) {
  reconnect_logs++;assert(!strstr(last_log,"private")&&!strstr(last_log,"secret"));
 }
 if(strstr(last_log,"GET endpoint="))
  snprintf(endpoint_log,sizeof(endpoint_log),"%s",last_log);
 va_end(args);log_calls++;
}
#define ESP_LOGW(tag,format,...) capture_log(format,__VA_ARGS__)
static const char *esp_err_to_name(esp_err_t err) {(void)err;return "TEST_ERR";}
static int64_t esp_timer_get_time(void) {return now_us;}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
 init_calls++;
 if (init_fails) return NULL;
 client.config=*config;client.status=status_code;return &client;
}
static esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,
                                            const char *name,const char *value) {
 (void)c;assert(strcmp(name,"Accept")==0&&strcmp(value,"application/json")==0);
 return ESP_OK;
}
static int esp_http_client_get_status_code(esp_http_client_handle_t c) {return c->status;}
static esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t c,int *code,int *flags) {
 (void)c;*code=tls_error_code;*flags=tls_error_flags;
 esp_err_t err=tls_error_result;tls_error_result=ESP_OK;tls_error_code=tls_error_flags=0;return err;
}
static esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c,int ms) {
 assert(ms>0&&ms<=15000);c->config.timeout_ms=ms;return ESP_OK;
}
static esp_err_t esp_http_client_close(esp_http_client_handle_t c) {(void)c;closed=true;return ESP_OK;}
static void vTaskDelay(unsigned ticks) {assert(ticks>=1&&ticks<=1000);now_us+=(int64_t)ticks*1000;}
static esp_err_t esp_http_client_perform(esp_http_client_handle_t c) {
 if(failed_attempts>0) {
  failed_attempts--;now_us+=failed_advance_us;c->status=-1;return ESP_ERR_HTTP_FETCH_HEADER;
 }
 now_us+=advance_us;
 // IDF may return EAGAIN for header waits in synchronous mode too.
 if(async_pending) {async_calls++;return ESP_ERR_HTTP_EAGAIN;}
 if (transport_error) return transport_error;
 if (retry_after) {
  esp_http_client_event_t header={.event_id=HTTP_EVENT_ON_HEADER,
   .header_key="Retry-After",.header_value=retry_after,
   .user_data=c->config.user_data,.client=c};
  esp_err_t err=c->config.event_handler(&header);if(err) return err;
  if(fail_after_header){c->status=-1;return ESP_ERR_HTTP_FETCH_HEADER;}
 }
 for (int i=0;i<chunk_count;i++) {
  now_us+=chunk_advance_us;
  esp_http_client_event_t data={.event_id=HTTP_EVENT_ON_DATA,
   .data=chunks[i],.data_len=(int)strlen(chunks[i]),
   .user_data=c->config.user_data,.client=c};
  c->config.event_handler(&data); // IDF ignores ON_DATA return values.
  body_timeout_ms[i]=c->config.timeout_ms;
  if(closed) return ESP_FAIL;
 }
 return fail_after_body?ESP_ERR_HTTP_CONNECTION_CLOSED:ESP_OK;
}
static void esp_http_client_cleanup(esp_http_client_handle_t c) {
 (void)c;cleanup_calls++;
}
static void *injected_realloc(void *p,size_t size) {
 if (realloc_fails) return NULL;
 return realloc(p,size);
}
#define realloc injected_realloc
static int64_t s_retry_at_us, s_jolpica_retry_at_us, s_deadline_us, s_request_at_us[3];
static const char *TAG="pdk_http_test";
'''
        code += types + '\n'
        code += next(line for line in source.splitlines()
                     if line.startswith('#define HTTP_REQUEST_TIMEOUT_US')) + '\n'
        for signature in ['void pdkpass_http_release(', 'static unsigned http_origin(', 'static void report_failure(',
                          'void pdkpass_http_report_data_failure(',
                          'bool pdkpass_http_expired(', 'static const char *http_resource(',
                          'static void report_request_failure(', 'static bool response_expired(',
                          'static int response_timeout_ms(',
                          'static bool stream_item(', 'static esp_err_t http_event(', 'static bool wait_for_request(',
                          'static esp_err_t perform(', 'esp_err_t pdkpass_http_get(',
                          'esp_err_t pdkpass_http_array(']:
            code += function(source, signature)
        code += r'''
static void reset_request(void) {
 status_code=200;transport_error=ESP_OK;retry_after=NULL;
 s_deadline_us=0;async_pending=closed=fail_after_body=fail_after_header=false;async_calls=0;advance_us=chunk_advance_us=0;
 failed_attempts=tls_error_code=tls_error_flags=reconnect_logs=recovery_logs=0;failed_advance_us=0;tls_error_result=ESP_OK;
 memset(body_timeout_ms,0,sizeof(body_timeout_ms));
 chunk_count=0;init_fails=false;realloc_fails=false;
 memset(s_request_at_us,0,sizeof(s_request_at_us));
 log_calls=memory_logs=0;last_log[0]='\0';endpoint_log[0]='\0';s_retry_at_us=0;s_jolpica_retry_at_us=0;
}
static bool reject_item(const cJSON *item,void *context) {(void)item;(void)context;return false;}
static bool accept_item(const cJSON *item,void *context) {
 assert(item&&item->object);(*(int *)context)++;return true;
}
int main(void) {
 char *json=NULL;
 reset_request();chunks[0]="{\"x\":";chunks[1]="1}";chunk_count=2;
 assert(pdkpass_http_get("https://example.test",32,&json)==ESP_OK);
 assert(strcmp(json,"{\"x\":1}")==0&&log_calls==0);free(json);
 reset_request();chunks[0]="12345";chunk_count=1;
 assert(pdkpass_http_get("https://example.test",4,&json)==ESP_ERR_INVALID_SIZE);
 assert(!json&&strstr(last_log,"stage=body-limit"));
 assert(!strstr(last_log,"heap(") && !strstr(last_log,"low="));
 reset_request();chunks[0]="ok";chunk_count=1;realloc_fails=true;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_NO_MEM);
 assert(!json&&strstr(last_log,"stage=body-alloc"));
 reset_request();transport_error=ESP_FAIL;status_code=0;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_FAIL);
 assert(strstr(last_log,"stage=transport")&&memory_logs==1);
 reset_request();transport_error=ESP_FAIL;status_code=0;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions?meeting_key=123",16,&json)==ESP_FAIL);
 assert(strstr(endpoint_log,"endpoint=sessions")&&!strstr(endpoint_log,"123"));
 reset_request();init_fails=true;int cleaned=cleanup_calls;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_NO_MEM);
 assert(strstr(last_log,"stage=client-init")&&cleanup_calls==cleaned);
 reset_request();status_code=500;chunks[0]="very long error page";chunk_count=1;
 assert(pdkpass_http_get("https://example.test",4,&json)==ESP_ERR_INVALID_RESPONSE);
 assert(strstr(last_log,"stage=http-status")&&!json);
 reset_request();now_us=1000000;status_code=429;retry_after="2";
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_INVALID_RESPONSE);
 assert(strstr(last_log,"stage=http-status")&&s_retry_at_us==3000000);
 int initialized=init_calls;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_TIMEOUT);
 assert(init_calls==initialized);
 status_code=200;retry_after=NULL;chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/driverstandings/",16,&json)==ESP_OK);free(json);
 assert(s_retry_at_us==3000000&&s_jolpica_retry_at_us==0);
 now_us=3000000;status_code=200;retry_after=NULL;chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_OK);free(json);
 reset_request();now_us=1000000;status_code=503;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_INVALID_RESPONSE);
 assert(s_retry_at_us==61000000);
 reset_request();now_us=1000000;status_code=429;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/driverstandings/",16,&json)==ESP_ERR_INVALID_RESPONSE);
 assert(s_jolpica_retry_at_us==61000000&&s_retry_at_us==0);
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/driverstandings/",16,&json)==ESP_ERR_TIMEOUT);
 status_code=200;chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://api.openf1.org/v1/meetings",16,&json)==ESP_OK);free(json);
 reset_request();chunks[0]="[{\"x\":1},";chunks[1]="{\"x\":2}]";chunk_count=2;
 int items=0;
 assert(pdkpass_http_array("https://example.test",accept_item,&items)==ESP_OK);
 assert(items==2&&log_calls==0);
 reset_request();realloc_fails=true;
 char many[10000];size_t used=0;many[used++]='[';
 for (int i=0;i<1200;i++) {
  if (i) many[used++]=',';
  memcpy(many+used,"{\"x\":1}",7);used+=7;
 }
 many[used++]=']';many[used]='\0';
 assert(used>8192);chunks[0]=many;chunk_count=1;items=0;
 assert(pdkpass_http_array("https://example.test",accept_item,&items)==ESP_OK);
 assert(items==1200&&log_calls==0);
 reset_request();chunks[0]="[{\"x\":1}";chunk_count=1;items=0;
 assert(pdkpass_http_array("https://example.test",accept_item,&items)==ESP_ERR_INVALID_RESPONSE);
 assert(items==1&&strstr(last_log,"stage=stream-end"));
 reset_request();chunks[0]="[oops]";chunk_count=1;
 assert(pdkpass_http_array("https://example.test",accept_item,&items)==ESP_ERR_INVALID_RESPONSE);
 assert(strstr(last_log,"stage=json-stream"));
 reset_request();pdkpass_http_report_data_failure("standings-json",ESP_ERR_INVALID_RESPONSE,2345);
 assert(strstr(last_log,"stage=standings-json")&&strstr(last_log,"bytes=2345"));
 assert(cleanup_calls>0);
 reset_request();now_us=100;s_deadline_us=100;
 int calls=init_calls;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_TIMEOUT);
 assert(init_calls==calls&&!json);
 // Automatic header waits must finish even if the SDK repeatedly yields EAGAIN.
 reset_request();now_us=0;async_pending=true;advance_us=15000000;
 s_transaction_active=true;int auto_cleaned=cleanup_calls;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions?token=private",16,&json)==ESP_ERR_TIMEOUT);
 assert(async_calls==2&&!json&&s_deadline_us==0&&!s_client);
 assert(now_us>=30000000&&now_us<30010000&&cleanup_calls==auto_cleaned+1);
 assert(strstr(last_log,"request-timeout")&&strstr(endpoint_log,"mode=automatic"));
 assert(!strstr(endpoint_log,"private")&&!strstr(endpoint_log,"token"));
 s_transaction_active=false;
 // A successful subsequent request gets a fresh deadline and connection.
 reset_request();chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_OK);free(json);
 // Manual requests also bound a stalled GET without changing the operation limit.
 reset_request();now_us=0;s_deadline_us=120000000;async_pending=true;advance_us=15000000;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/constructorstandings/?secret=x",16,&json)==ESP_ERR_TIMEOUT);
 assert(async_calls==2&&s_deadline_us==120000000&&!json);
 assert(strstr(last_log,"request-timeout")&&strstr(endpoint_log,"constructorstandings"));
 assert(strstr(endpoint_log,"mode=manual")&&strstr(endpoint_log,"origin=jolpica"));
 assert(!strstr(endpoint_log,"secret"));
 // Body callbacks must stop a trickling automatic response at the same limit.
 reset_request();now_us=0;advance_us=30000000;chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_TIMEOUT);
 assert(closed&&!json&&strstr(last_log,"request-timeout"));
 reset_request();now_us=0;advance_us=27000000;chunk_advance_us=2000000;
 chunks[0]="a";chunks[1]="b";chunk_count=2;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_TIMEOUT);
 assert(body_timeout_ms[0]==1000&&closed&&!json);
 reset_request();now_us=100;s_deadline_us=1000100;async_pending=true;advance_us=500000;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_TIMEOUT);
 assert(async_calls==2&&!json&&strstr(last_log,"operation-timeout"));
 reset_request();now_us=100;s_deadline_us=500100;advance_us=500000;
 chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_TIMEOUT);
 assert(closed&&!json&&strstr(last_log,"operation-timeout"));
 reset_request();chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_OK);free(json);
 s_transaction_active=true;int init_before=init_calls,clean_before=cleanup_calls;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_OK);free(json);
 assert(s_client&&client.config.keep_alive_enable&&!client.config.user_data);
 assert(pdkpass_http_get("https://api.openf1.org/v1/drivers",16,&json)==ESP_OK);free(json);
 assert(init_calls==init_before+1&&cleanup_calls==clean_before&&!client.config.user_data);
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/",16,&json)==ESP_OK);free(json);
 assert(init_calls==init_before+2&&cleanup_calls==clean_before+1&&s_client_origin==2);
 transport_error=ESP_FAIL;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/",16,&json)==ESP_FAIL&&!s_client);
 assert(cleanup_calls==clean_before+2);transport_error=ESP_OK;
 assert(pdkpass_http_get("https://api.openf1.org/v1/drivers",16,&json)==ESP_OK);free(json);
 pdkpass_http_release();assert(!s_client&&cleanup_calls==clean_before+3);

 reset_request();now_us=0;chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_OK);free(json);
 assert(now_us==0&&s_request_at_us[1]==2100000);
 assert(pdkpass_http_get("https://api.openf1.org/v1/drivers",16,&json)==ESP_OK);free(json);
 assert(now_us==2100000); // Shared pacing persists across resources.
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/",16,&json)==ESP_OK);free(json);
 assert(now_us==2100000); // Each API has an independent clock.
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/",16,&json)==ESP_OK);free(json);
 assert(now_us==9400000);
 reset_request();now_us=1000000;s_deadline_us=121000000;s_retry_at_us=61000000;
 chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://api.openf1.org/v1/drivers",16,&json)==ESP_OK);free(json);
 assert(now_us==61000000&&s_deadline_us==121000000); // Manual wait preserves its original deadline.
 reset_request();now_us=100;s_deadline_us=1000100;s_request_at_us[1]=2000100;
 calls=init_calls;
 assert(pdkpass_http_get("https://api.openf1.org/v1/drivers",16,&json)==ESP_ERR_TIMEOUT);
 assert(init_calls==calls&&!json&&strstr(last_log,"request-wait"));
 reset_request();chunks[0]="[{}]";chunk_count=1;
 assert(pdkpass_http_array("https://example.test",reject_item,NULL)==ESP_ERR_INVALID_RESPONSE);
 assert(strstr(last_log,"stage=item-rejected"));
 // A stale reused socket recovers once on a fresh TLS client, respecting pacing.
 reset_request();now_us=0;s_deadline_us=120000000;s_transaction_active=true;
 chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/driverstandings/",16,&json)==ESP_OK);free(json);
 int recovery_inits=init_calls,recovery_cleanups=cleanup_calls;
 failed_attempts=1;failed_advance_us=1000000;tls_error_code=-MBEDTLS_ERR_NET_RECV_FAILED;tls_error_result=ESP_FAIL;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/constructorstandings/?secret=private",16,&json)==ESP_OK);
 assert(json&&strcmp(json,"ok")==0);free(json);
 assert(init_calls==recovery_inits+1&&cleanup_calls==recovery_cleanups+1&&reconnect_logs==1&&recovery_logs==1);
 assert(now_us==14600000&&s_deadline_us==120000000);
 pdkpass_http_release();s_transaction_active=false;
 // Signed network errors (as used by handshake paths) also recover.
 reset_request();now_us=0;chunks[0]="ok";chunk_count=1;failed_attempts=1;
 tls_error_code=MBEDTLS_ERR_NET_CONN_RESET;tls_error_result=ESP_FAIL;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_OK);free(json);
 assert(reconnect_logs==1&&recovery_logs==1);
 // Permanent transport failure terminates after exactly two connections.
 reset_request();now_us=0;failed_attempts=2;recovery_inits=init_calls;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_ERR_HTTP_FETCH_HEADER);
 assert(!json&&init_calls==recovery_inits+2&&reconnect_logs==1&&failed_attempts==0);
 // TLS allocation/certificate failures do not immediately retry.
 int excluded[]={-0x7F00,-0x2700,0,0x7F00,0x2700};
 for(unsigned i=0;i<5;i++) {
  reset_request();now_us=0;failed_attempts=1;tls_error_code=excluded[i];
  if(i==2) tls_error_result=ESP_ERR_NO_MEM;
  recovery_inits=init_calls;
  assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_ERR_HTTP_FETCH_HEADER);
  assert(!json&&init_calls==recovery_inits+1&&!reconnect_logs);
 }
 reset_request();failed_attempts=1;tls_error_flags=1;recovery_inits=init_calls;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_ERR_HTTP_FETCH_HEADER);
 assert(init_calls==recovery_inits+1&&!reconnect_logs);
 // A consumer that saw any streamed data is never replayed.
 reset_request();now_us=0;chunks[0]="[{\"x\":1}]";chunk_count=1;fail_after_body=true;items=0;
 recovery_inits=init_calls;
 assert(pdkpass_http_array("https://api.openf1.org/v1/sessions",accept_item,&items)==ESP_ERR_HTTP_CONNECTION_CLOSED);
 assert(items==1&&init_calls==recovery_inits+1&&!reconnect_logs);
 // Even an empty response with a header is not replayed.
 reset_request();now_us=0;retry_after="0";fail_after_header=true;recovery_inits=init_calls;
 assert(pdkpass_http_get("https://api.openf1.org/v1/sessions",16,&json)==ESP_ERR_HTTP_FETCH_HEADER);
 assert(init_calls==recovery_inits+1&&!reconnect_logs&&!json);
 // Retry pacing cannot wait past a shorter accepted manual deadline.
 reset_request();now_us=0;s_deadline_us=5000000;failed_attempts=1;recovery_inits=init_calls;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/constructorstandings/",16,&json)==ESP_ERR_TIMEOUT);
 assert(init_calls==recovery_inits+1&&now_us==0&&s_deadline_us==5000000&&!json);
 // Both attempts share the original 30-second network deadline.
 reset_request();now_us=0;failed_attempts=1;failed_advance_us=20000000;
 async_pending=true;advance_us=5000000;recovery_inits=init_calls;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/constructorstandings/",16,&json)==ESP_ERR_TIMEOUT);
 assert(!json&&init_calls==recovery_inits+2&&async_calls==2&&reconnect_logs==1);
 assert(now_us>=30000000&&now_us<30010000&&strstr(last_log,"request-timeout"));
 // A shorter manual deadline still takes precedence across reconnection.
 reset_request();now_us=0;s_deadline_us=25000000;failed_attempts=1;failed_advance_us=20000000;
 async_pending=true;advance_us=5000000;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/constructorstandings/",16,&json)==ESP_ERR_TIMEOUT);
 assert(async_calls==1&&s_deadline_us==25000000&&strstr(last_log,"operation-timeout"));
 // Response headers and HTTP rate limits do not trigger connection replay.
 reset_request();now_us=0;status_code=429;retry_after="60";recovery_inits=init_calls;
 assert(pdkpass_http_get("https://api.jolpi.ca/ergast/f1/2026/constructorstandings/",16,&json)==ESP_ERR_INVALID_RESPONSE);
 assert(init_calls==recovery_inits+1&&!reconnect_logs&&s_jolpica_retry_at_us==60000000);
 puts("HTTP: fresh-connection recovery, excluded failures, original deadlines and pacing: PASS");
}
'''
        compile_run(code, ['main/pdkpass_json_stream.c'])

    def test_streamed_results_publish_only_complete_responses(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        cache_types = source[source.index('typedef struct {'):
                             source.index('} race_cache_t;') + len('} race_cache_t;')]
        driver_type = source[source.index('typedef struct {\n    unsigned position;'):
                             source.index('} result_driver_t;') + len('} result_driver_t;')]
        discovery_type = source[source.index('typedef struct {\n    race_cache_t *cache;'):
                                source.index('} session_discovery_t;') + len('} session_discovery_t;')]
        details_type = source[source.index('typedef struct {\n    const result_driver_t *top;'):
                              source.index('} podium_details_t;') + len('} podium_details_t;')]
        window_define = next(line for line in source.splitlines()
                             if line.startswith('#define RESULTS_WINDOW_SECONDS'))
        code = PRELUDE + '#include "pdkpass_reminder.h"\n' + window_define + '\n' + cache_types + '\n' + driver_type + '\n' + discovery_type + '\n' + details_type + r'''
typedef struct cJSON {
 const char *key, *valuestring;
 int type, valueint;
 double valuedouble;
 struct cJSON *child, *next;
} cJSON;
static const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *obj,const char *key) {
 for(const cJSON *p=obj?obj->child:NULL;p;p=p->next)
  if(strcmp(p->key,key)==0)return p;
 return NULL;
}
static bool cJSON_IsString(const cJSON *p) {return p&&p->type==1;}
static bool cJSON_IsNumber(const cJSON *p) {return p&&p->type==2;}
static bool cJSON_IsTrue(const cJSON *p) {return p&&p->type==3;}
static bool fail_stream, cancelled_schedule, missing_start;
static int reminder_updates;
void pdkpass_reminder_update_round(unsigned year,unsigned round,int32_t meeting_key,const pdkpass_reminder_entry_t *entries) {
 assert(year==2026&&round==1&&meeting_key==42);reminder_updates++;
 assert(entries[PDKPASS_SESSION_RACE].session_key==123);
 assert((entries[PDKPASS_SESSION_RACE].flags!=0)==cancelled_schedule);
 if(!cancelled_schedule)assert(entries[PDKPASS_SESSION_RACE].start_utc==900);
}
static int driver_requests, fail_driver, missing_driver;
static bool duplicate_driver, conflicting_driver;
bool pdkpass_parse_iso8601_utc(const char *text,int64_t *out) {
 *out=strcmp(text,"start")==0?900:1000;return true;
}
pdkpass_session_kind_t pdkpass_session_kind_from_name(const char *name) {
 return strcmp(name,"Race")==0?PDKPASS_SESSION_RACE:PDKPASS_SESSION_COUNT;
}
static int pdkpass_http_array(const char *url,bool (*item)(const cJSON *,void *),void *ctx) {
 if(strstr(url,"/sessions?")) {
  cJSON a={.key="session_name",.valuestring="Race",.type=1};
  cJSON b={.key="session_key",.valueint=123,.valuedouble=123,.type=2};
  cJSON c={.key="date_end",.valuestring="date",.type=1};
  cJSON d={.key="date_start",.valuestring="start",.type=missing_start?0:1};
  cJSON e={.key="is_cancelled",.type=cancelled_schedule?3:0};
  cJSON m={.key="meeting_key",.valueint=42,.valuedouble=42,.type=2};
  a.next=&b;b.next=&c;c.next=&d;d.next=&e;e.next=&m;cJSON obj={.child=&a};
  assert(item(&obj,ctx));return fail_stream?ESP_FAIL:ESP_OK;
 }
 if(strstr(url,"/session_result?")) {
  for(int i=1;i<=3;i++) {
   cJSON a={.key="position",.valueint=i,.valuedouble=i,.type=2};
   cJSON b={.key="driver_number",.valueint=i,.valuedouble=i,.type=2};
   a.next=&b;cJSON obj={.child=&a};assert(item(&obj,ctx));
  }
  return ESP_OK;
 }
 assert(strstr(url,"/drivers?session_key=123&driver_number="));
 const char *filter=strstr(url,"&driver_number=");
 int i=atoi(filter+strlen("&driver_number="));
 assert(i>=1 && i<=3); driver_requests++;
 const char *codes[]={"AAA","BBB","CCC"};
 if(i==missing_driver)return ESP_OK;
 cJSON a={.key="driver_number",.valueint=i,.valuedouble=i,.type=2};
 cJSON b={.key="name_acronym",.valuestring=codes[i-1],.type=1};
 a.next=&b;cJSON obj={.child=&a};assert(item(&obj,ctx));
 if(duplicate_driver) {
  if(conflicting_driver)b.valuestring="BAD";
  if(!item(&obj,ctx))return ESP_FAIL;
 }
 return fail_stream || i==fail_driver ? ESP_FAIL : ESP_OK;
}
'''
        for signature in ['static bool json_positive_key(', 'static bool json_bool(', 'static void update_session_identity(',
                          'static void parse_reminder_session(', 'static bool parse_discovered_session(', 'static bool discover_sessions(',
                          'static bool parse_podium_item(', 'static bool podium_complete(',
                          'static void copy_json_text(', 'static bool parse_podium_driver(',
                          'static bool podium_drivers_complete(', 'static bool fetch_result(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 races[0].switch_at_utc=2000;races[0].meeting_key=42;races[0].round=1;
 race_cache_t cache={0};fail_stream=true;
 assert(!discover_sessions(0,&cache,2000));
 assert(!cache.sessions[PDKPASS_SESSION_RACE].present&&reminder_updates==0);
 fail_stream=false;assert(discover_sessions(0,&cache,2000));
 assert(reminder_updates==1);
 missing_start=true;assert(discover_sessions(0,&cache,2000));assert(reminder_updates==1);
 cancelled_schedule=true;assert(discover_sessions(0,&cache,2000));assert(reminder_updates==2);
 cancelled_schedule=missing_start=false;
 session_cache_t *session=&cache.sessions[PDKPASS_SESSION_RACE];
 assert(session->present&&session->session_key==123);
 strcpy(session->podium_codes[0],"OLD");fail_stream=true;
 assert(!fetch_result(session));assert(!session->ready);
 assert(strcmp(session->podium_codes[0],"OLD")==0);
 fail_stream=false;
 for(int i=1;i<=3;i++) {
  fail_driver=i;driver_requests=0;
  assert(!fetch_result(session));assert(!session->ready);
  assert(driver_requests==i && strcmp(session->podium_codes[0],"OLD")==0);
 }
 fail_driver=0;missing_driver=3;
 assert(!fetch_result(session));assert(!session->ready);
 assert(strcmp(session->podium_codes[0],"OLD")==0);
 missing_driver=0;driver_requests=0;
 assert(fetch_result(session));assert(session->ready && driver_requests==3);
 assert(strcmp(session->podium_codes[0],"AAA")==0);
 assert(strcmp(session->podium_codes[1],"BBB")==0);
 assert(strcmp(session->podium_codes[2],"CCC")==0);

 duplicate_driver=true;session->ready=0;driver_requests=0;
 assert(fetch_result(session)&&session->ready&&driver_requests==3);
 conflicting_driver=true;session->ready=0;
 assert(!fetch_result(session)&&!session->ready);
 assert(!strcmp(session->podium_codes[0],"AAA")); // Valid old cache survives conflicting response.
 duplicate_driver=conflicting_driver=false;
 cJSON key={.type=2,.valuedouble=1};int32_t parsed_key;
 assert(json_positive_key(&key,&parsed_key)&&parsed_key==1);
 double invalid[]={0,-1,1.5,2147483648.0};
 for(unsigned i=0;i<4;i++){key.valuedouble=invalid[i];assert(!json_positive_key(&key,&parsed_key));}
 result_driver_t top[3]={0};cJSON pos={.key="position",.type=2,.valuedouble=1};
 cJSON num={.key="driver_number",.type=2,.valuedouble=44};pos.next=&num;cJSON obj={.child=&pos};
 assert(parse_podium_item(&obj,top));assert(!parse_podium_item(&obj,top));
 pos.valuedouble=2;assert(!parse_podium_item(&obj,top));num.valuedouble=100;assert(!parse_podium_item(&obj,top));
 cJSON name={.key="session_name",.type=1,.valuestring="Race"};
 key.key="session_key";key.valuedouble=321;cJSON meet={.key="meeting_key",.type=2,.valuedouble=99};
 cJSON end={.key="date_end",.type=1,.valuestring="date"};name.next=&key;key.next=&meet;meet.next=&end;
 obj.child=&name;race_cache_t partial={0};session_discovery_t d={.cache=&partial,.valid=true,.meeting_key=42,.window_end=2000};
 assert(!parse_discovered_session(&obj,&d)&&!d.valid&&!partial.sessions[PDKPASS_SESSION_RACE].present);
 meet.valuedouble=42;d.valid=true;assert(parse_discovered_session(&obj,&d));
 assert(!parse_discovered_session(&obj,&d)&&!d.valid);
 puts("streamed results commit only complete responses: PASS");
}
'''
        compile_run(code)

    def test_jolpica_standings_atomic_pages_and_validation(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE + '#include <ctype.h>\n' + r'''
#define ESP_ERR_INVALID_RESPONSE 0x108
typedef struct cJSON { char key[40], valuestring[96]; int type; struct cJSON *child,*next; } cJSON;
static const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *p,const char *key) {
 for(p=p?p->child:NULL;p;p=p->next)if(strcmp(p->key,key)==0)return p;return NULL;
}
static bool cJSON_IsString(const cJSON *p) {return p&&p->type==1;}
static bool cJSON_IsArray(const cJSON *p) {return p&&p->type==2;}
static int cJSON_GetArraySize(const cJSON *p) {int n=0;for(p=p?p->child:NULL;p;p=p->next)n++;return n;}
static const cJSON *cJSON_GetArrayItem(const cJSON *p,int i) {p=p?p->child:NULL;while(p&&i--)p=p->next;return p;}
static cJSON *node(cJSON *parent,const char *key,int type,const char *text) {
 cJSON *p=calloc(1,sizeof(*p));assert(p);p->type=type;
 snprintf(p->key,sizeof(p->key),"%s",key?key:"");
 snprintf(p->valuestring,sizeof(p->valuestring),"%s",text?text:"");
 if(parent){cJSON **tail=&parent->child;while(*tail)tail=&(*tail)->next;*tail=p;}return p;
}
static void cJSON_Delete(cJSON *p) {if(!p)return;cJSON *n=p->child;while(n){cJSON *next=n->next;cJSON_Delete(n);n=next;}free(p);}
static int mode, calls, pauses;
static cJSON *pending_root;
static void vTaskDelay(unsigned ms) {assert(ms==300);pauses++;}
static void pdkpass_http_report_data_failure(const char *stage,esp_err_t err,size_t bytes) {(void)stage;(void)err;(void)bytes;}
static cJSON *cJSON_ParseWithOpts(const char *text,const char **end,bool strict) {
 (void)text;(void)end;assert(strict);cJSON *p=pending_root;pending_root=NULL;return p;
}
static int pdkpass_http_get(const char *url,size_t limit,char **body) {
 calls++;assert(limit==4096);assert(strstr(url,"https://api.jolpi.ca/ergast/f1/2026/"));
 *body=NULL;
 if((mode==1&&calls==2)||(mode==2&&calls==3))return ESP_FAIL;
 *body=malloc(3);strcpy(*body,"{}");
 if(mode==3)return ESP_OK; // Invalid JSON from a successful HTTP request.
 cJSON *root=node(NULL,NULL,3,NULL);pending_root=root;
 cJSON *mr=node(root,"MRData",3,NULL);
 if(calls<=2) {
  assert(strstr(url,calls==1?"/2026/driverstandings/?limit=4&offset=0":"/2026/15/driverstandings/?limit=4&offset=4"));
  node(mr,"total",1,mode==4&&calls==2?"5":"6");
  node(mr,"offset",1,mode==5?"9":calls==1?"0":"4");
  node(mr,"limit",1,mode==6?"30":"4");
  cJSON *table=node(mr,"StandingsTable",3,NULL),*lists=node(table,"StandingsLists",2,NULL);
  cJSON *list=node(lists,NULL,3,NULL);
  node(list,"season",1,mode==7?"2025":"2026");
  node(list,"round",1,mode==8&&calls==2?"16":"15");
  cJSON *rows=node(list,"DriverStandings",mode==9?3:2,NULL);
  int start=calls==1?0:4,end=calls==1?4:6;
  if(mode==10)end=start; // Empty or truncated page is not a complete snapshot.
  for(int i=start;i<end;i++) {
   cJSON *row=node(rows,NULL,3,NULL);char number[16],position[16],code[4]={'A','A',(char)('A'+i),0};
   snprintf(number,sizeof(number),"%d",i==0||mode==11?12:i+1);
   snprintf(position,sizeof(position),"%d",mode==12?1:i+1);
   node(row,"position",1,position);
   const char *points=i==0?"292":i==1?"25.5":"18";
   if(i==0){if(mode==13)points="-1";if(mode==14)points="7000";if(mode==15)points="292junk";
    if(mode==16)points=".5";if(mode==17)points="NaN";if(mode==18)points="25.55";}
   node(row,"points",mode==19?3:1,points);
   cJSON *driver=node(row,"Driver",3,NULL);
   node(driver,"permanentNumber",1,number);node(driver,"code",1,i==0||mode==20?"ANT":code);
   node(driver,"familyName",1,i==0?"Antonelli":"Beta");
   if(mode!=21)node(driver,"givenName",1,i==0?"Andrea Kimi":"Bob");
   cJSON *teams=node(row,"Constructors",2,NULL),*team=node(teams,NULL,3,NULL);
   node(team,"constructorId",1,"mercedes");node(team,"name",1,"Mercedes");
   if(i==5){team=node(teams,NULL,3,NULL);node(team,"constructorId",1,"ferrari");node(team,"name",1,"Ferrari");}
  }
 } else {
  assert(calls==3&&strstr(url,"/2026/15/")&&!strstr(url,"driverstandings"));
  cJSON *table=node(mr,"RaceTable",3,NULL),*rows=node(table,"Races",2,NULL),*race=node(rows,NULL,3,NULL);
  node(race,"season",1,mode==22?"2025":"2026");node(race,"round",1,mode==23?"14":"15");
  node(race,"date",1,mode==24?"2026-08-01":mode==25?"2026-12-01":"2026-09-26");
  node(race,"time",1,"11:00:00Z");
 }
 return ESP_OK;
}
'''
        for signature in ['static void copy_text(', 'static void copy_upper(',
                          'static void fill_known_first_name(', 'static const char *json_string(',
                          'static bool jolpica_decimal(', 'static bool jolpica_uint(',
                          'static cJSON *jolpica_get(', 'static void jolpica_constructor(', 'static void jolpica_team(',
                          'static bool jolpica_driver(', 'static unsigned standings_date_order(',
                          'static bool jolpica_standings_date(', 'static bool fetch_driver_standings(',
                          'static bool fetch_standings(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 pdkpass_season_snapshot_t candidate={.year=2026,.driver_count=1};
 strcpy(candidate.drivers[0].code,"ANT");candidate.drivers[0].points_tenths=2420;
 strcpy(candidate.standings_as_of,"31 AUG");pdkpass_season_snapshot_t old=candidate;
 int64_t now;assert(pdkpass_parse_iso8601_utc("2026-09-26T16:00:00Z",&now));
 for(mode=1;mode<=25;mode++) {
  calls=pauses=0;assert(!fetch_standings(now,&candidate));
  assert(memcmp(&candidate,&old,sizeof(old))==0);assert(!pending_root);
 }
 mode=0;calls=pauses=0;assert(fetch_standings(now,&candidate));
 assert(calls==3&&pauses==2&&candidate.driver_count==6);
 assert(candidate.drivers[0].points_tenths==2920&&candidate.drivers[1].points_tenths==255);
 assert(strcmp(candidate.drivers[0].first_name,"KIMI")==0);
 assert(strcmp(candidate.drivers[0].team,"MERCEDES")==0&&candidate.drivers[0].accent==0x00A19C);
 assert(strcmp(candidate.drivers[5].team,"MULTIPLE TEAMS")==0);
 assert(strcmp(candidate.standings_as_of,"26 SEP")==0);
 calls=pauses=0;assert(fetch_standings(now-12*3600,&candidate)); // Published points before the scheduled race.
 puts("Jolpica: complete pages, pinned round, decimal points, metadata, atomic failures: PASS");
}
'''
        compile_run(code, ['main/pdkpass_data.c', 'main/pdkpass_results_core.c', 'main/pdkpass_season_core.c'])

    def test_jolpica_sync_survives_openf1_failure(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE + r'''
static unsigned marks[PDKPASS_SYNC_STATUS_COUNT];
void pdkpass_sync_mark_success(pdkpass_sync_status_t status,int64_t utc) {
 (void)utc;assert((unsigned)status<PDKPASS_SYNC_STATUS_COUNT);marks[status]++;
}
#define ESP_ERR_NO_MEM 0x101
static pdkpass_season_snapshot_t s_season;
static bool s_has_cached_data,calendar_ok,points_ok,save_ok=true;
static int saves,callbacks,fetches;
static void callback(void) {callbacks++;}
static pdkpass_season_callback_t s_callback=callback;
bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *out) {*out=s_season;return true;}
static void pdkpass_http_report_data_failure(const char *stage,esp_err_t err,size_t bytes) {(void)stage;(void)err;(void)bytes;}
static bool build_candidate(unsigned year,const pdkpass_season_snapshot_t *current,pdkpass_season_snapshot_t **out) {
 assert(current==&s_season);*out=NULL;if(!calendar_ok)return false;
 *out=malloc(sizeof(**out));assert(*out);**out=*current;
 (*out)->year=year;(*out)->races[0].laps=60;return true;
}
static bool fetch_standings(int64_t now,pdkpass_season_snapshot_t *out) {
 (void)now;fetches++;assert(out->year==2026);if(!points_ok)return false;
 out->drivers[0].points_tenths=2920;strcpy(out->standings_as_of,"26 SEP");return true;
}
static esp_err_t save_cache(const pdkpass_season_snapshot_t *out) {(void)out;saves++;return save_ok?ESP_OK:ESP_FAIL;}
static bool synchronize_teams(unsigned year,int64_t now) {(void)year;(void)now;return true;}
'''
        code += function(source, 'static bool synchronize(')
        code += r'''
int main(void) {
 int64_t now;assert(pdkpass_parse_iso8601_utc("2026-09-26T16:00:00Z",&now));
 s_season.year=2026;s_season.race_count=1;s_season.driver_count=1;
 s_season.races[0].meeting_key=99;s_season.drivers[0].points_tenths=2420;
 strcpy(s_season.standings_as_of,"31 AUG");pdkpass_season_snapshot_t old=s_season;
 points_ok=true;save_ok=false;assert(!synchronize(now));assert(!marks[0]&&!marks[2]);assert(!memcmp(&old,&s_season,sizeof(old))&&!callbacks);
 save_ok=true;assert(!synchronize(now)); // Calendar still needs a retry, but points publish.
 assert(s_season.drivers[0].points_tenths==2920&&s_season.races[0].meeting_key==99&&callbacks==1);
 assert(s_has_cached_data&&fetches==2);assert(!marks[0]&&marks[2]==1);
 old=s_season;points_ok=false;assert(!synchronize(now));assert(!memcmp(&old,&s_season,sizeof(old)));
 calendar_ok=true;assert(!synchronize(now));assert(s_season.races[0].laps==60&&s_season.drivers[0].points_tenths==2920);
 points_ok=true;assert(synchronize(now));assert(marks[0]==2&&marks[2]==2);
 int previous=fetches;calendar_ok=false;assert(!synchronize(now+366LL*86400));assert(fetches==previous);
 puts("Independent Jolpica sync, retry and persisted atomic publication: PASS");
}
'''
        compile_run(code, ['main/pdkpass_results_core.c', 'main/pdkpass_season_core.c'])

    def test_jolpica_constructor_pages(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        # Reuse the small JSON tree fake from the driver parser's test harness.
        test_source = Path(__file__).read_text()
        start = test_source.index('typedef struct cJSON { char key[40]')
        end = test_source.index('static int mode, calls, pauses;', start)
        code = PRELUDE + '#include <ctype.h>\n#define ESP_ERR_INVALID_RESPONSE 0x108\n' + test_source[start:end] + r'''
static int calls,mode,pauses;
static cJSON *pending_root;
static void vTaskDelay(unsigned ms) {assert(ms==300);pauses++;}
static void pdkpass_http_report_data_failure(const char *s,esp_err_t e,size_t n) {(void)s;(void)e;(void)n;}
static cJSON *cJSON_ParseWithOpts(const char *s,const char **e,bool strict) {
 (void)s;(void)e;assert(strict);cJSON *p=pending_root;pending_root=NULL;return p;
}
static int pdkpass_http_get(const char *url,size_t limit,char **body) {
 calls++;assert(limit==4096);assert(strstr(url,"https://api.jolpi.ca/"));*body=NULL;
 if(mode==calls)return ESP_FAIL;
 *body=malloc(3);strcpy(*body,"{}");if(mode==4)return ESP_OK;
 cJSON *root=node(NULL,NULL,3,NULL);pending_root=root;cJSON *mr=node(root,"MRData",3,NULL);
 if(calls<3) {
  assert(strstr(url,calls==1?"/2026/constructorstandings/?limit=4&offset=0":"/2026/15/constructorstandings/?limit=4&offset=4"));
  node(mr,"total",1,mode==5&&calls==2?"7":"6");
  node(mr,"limit",1,"4");node(mr,"offset",1,mode==6?"0":calls==1?"0":"4");
  cJSON *table=node(mr,"StandingsTable",3,NULL),*lists=node(table,"StandingsLists",2,NULL),*list=node(lists,NULL,3,NULL);
  node(list,"season",1,mode==7?"2025":"2026");node(list,"round",1,mode==8&&calls==2?"16":"15");
  cJSON *rows=node(list,"ConstructorStandings",2,NULL);
  int start=calls==1?0:4,end=calls==1?4:6;if(mode==9)end--;
  for(int i=start;i<end;i++) {
   char rank[4],id[16];snprintf(rank,sizeof(rank),"%d",i+1);snprintf(id,sizeof(id),"team%d",i);
   cJSON *row=node(rows,NULL,3,NULL);node(row,"position",1,mode==10?"1":rank);
   node(row,"points",1,mode==11?"-1":mode==12?"7000":mode==13?"503xyz":i==0?"503":"10.5");
   cJSON *team=node(row,"Constructor",3,NULL);
   node(team,"constructorId",1,i==0||mode==14?"mercedes":id);
   if(mode!=15)node(team,"name",1,i==0?"Mercedes":"New team");
  }
 } else {
  assert(calls==3&&!strstr(url,"constructorstandings"));
  cJSON *table=node(mr,"RaceTable",3,NULL),*rows=node(table,"Races",2,NULL),*race=node(rows,NULL,3,NULL);
  node(race,"season",1,"2026");node(race,"round",1,mode==16?"14":"15");
  node(race,"date",1,mode==17?"2026-08-01":"2026-09-26");node(race,"time",1,"11:00:00Z");
 }
 return ESP_OK;
}
'''
        for sig in ['static void copy_text(', 'static void copy_upper(',
                    'static const char *json_string(', 'static bool jolpica_decimal(',
                    'static bool jolpica_uint(', 'static cJSON *jolpica_get(',
                    'static void jolpica_constructor(', 'static unsigned standings_date_order(',
                    'static bool jolpica_standings_date(', 'static bool jolpica_team_item(',
                    'static bool fetch_team_standings(']:
            code += function(source, sig)
        code += r'''
int main(void) {
 pdkpass_team_snapshot_t candidate={.year=2026,.count=1};strcpy(candidate.as_of,"31 AUG");
 candidate.teams[0].points_tenths=4000;pdkpass_team_snapshot_t old=candidate;
 int64_t now;assert(pdkpass_parse_iso8601_utc("2026-09-26T16:00:00Z",&now));
 for(mode=1;mode<=17;mode++) {
  calls=pauses=0;assert(!fetch_team_standings(now,&candidate));
  assert(memcmp(&old,&candidate,sizeof(old))==0&&!pending_root);
 }
 mode=0;calls=pauses=0;assert(fetch_team_standings(now,&candidate));
 assert(candidate.count==6&&calls==3&&pauses==2);
 assert(candidate.teams[0].points_tenths==5030&&candidate.teams[1].points_tenths==105);
 assert(!strcmp(candidate.teams[0].name,"MERCEDES")&&candidate.teams[0].accent==0x00A19C);
 assert(!strcmp(candidate.teams[1].name,"NEW TEAM"));assert(!strcmp(candidate.as_of,"26 SEP"));
 puts("Constructor totals: pinned pagination, authoritative scores and atomic failures: PASS");
}
'''
        compile_run(code, ['main/pdkpass_results_core.c', 'main/pdkpass_season_core.c'])

    def test_team_cache_persistence_and_season_isolation(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        types = source[source.index('#define TEAM_CACHE_MAGIC'):source.index('static bool s_has_cached_data;')]
        code = PRELUDE + types + r'''
static unsigned marks[PDKPASS_SYNC_STATUS_COUNT];
void pdkpass_sync_mark_success(pdkpass_sync_status_t status,int64_t utc) {
 (void)utc;assert((unsigned)status<PDKPASS_SYNC_STATUS_COUNT);marks[status]++;
}
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
static const char *NVS_NAMESPACE="test";
static pdkpass_season_snapshot_t s_season={.year=2026};
static pdkpass_team_snapshot_t s_teams;
static team_cache_t disk;
static bool available,commit_ok=true,fetch_ok=true;
static int writes,commits,callbacks,fetches;
static void callback(void) {callbacks++;}
static pdkpass_season_callback_t s_callback=callback;
static esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *out) {(void)ns;(void)mode;*out=1;return ESP_OK;}
static void nvs_close(nvs_handle_t h) {(void)h;}
static esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
 (void)h;assert(!strcmp(key,"teams"));if(!available)return ESP_FAIL;
 assert(*size>=sizeof(disk));memcpy(out,&disk,sizeof(disk));*size=sizeof(disk);return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size) {
 (void)h;assert(!strcmp(key,"teams")&&size==sizeof(disk));writes++;
 if(commit_ok)memcpy(&disk,data,size);return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h) {(void)h;commits++;if(commit_ok)available=true;return commit_ok?ESP_OK:ESP_FAIL;}
static bool fetch_team_standings(int64_t now,pdkpass_team_snapshot_t *candidate) {
 (void)now;fetches++;if(!fetch_ok)return false;
 candidate->count=1;strcpy(candidate->as_of,"26 SEP");
 candidate->teams[0]=(pdkpass_team_t){.position=1,.points_tenths=5030};
 strcpy(candidate->teams[0].id,"mercedes");strcpy(candidate->teams[0].name,"MERCEDES");return true;
}
'''
        for sig in ['static void copy_text(', 'static bool jolpica_decimal(',
                    'static unsigned standings_date_order(', 'static bool team_snapshot_valid(',
                    'static void load_team_cache(', 'static esp_err_t save_team_cache(',
                    'bool pdkpass_season_team_snapshot(', 'static bool synchronize_teams(']:
            code += function(source, sig)
        code += r'''
int main(void) {
 s_lock=1;pdkpass_team_snapshot_t out;load_team_cache();assert(pdkpass_season_team_snapshot(&out)&&!out.count&&out.year==2026);
 commit_ok=false;assert(!synchronize_teams(2026,1));assert(!s_teams.count&&!callbacks);
 commit_ok=true;assert(synchronize_teams(2026,1));assert(s_teams.count==1&&callbacks==1);
 int saved=writes;assert(synchronize_teams(2026,1));assert(writes==saved&&callbacks==1);
 memset(&s_teams,0,sizeof(s_teams));load_team_cache();assert(s_teams.teams[0].points_tenths==5030);
 fetch_ok=false;assert(!synchronize_teams(2026,1));assert(s_teams.teams[0].points_tenths==5030);
 s_season.year=2027;assert(pdkpass_season_team_snapshot(&out)&&out.year==2027&&!out.count&&!strcmp(out.as_of,"PENDING"));
 int tried=fetches;assert(!synchronize_teams(2026,1)&&fetches==tried);
 fetch_ok=true;assert(synchronize_teams(2027,1));assert(s_teams.year==2027);
 assert(marks[PDKPASS_SYNC_STATUS_TEAMS]==3&&!marks[PDKPASS_SYNC_STATUS_CALENDAR]);
 disk.version=99;load_team_cache();assert(!s_teams.count);
 disk.version=1;disk.snapshot.count=PDKPASS_MAX_TEAMS+1;load_team_cache();assert(!s_teams.count);
 disk.snapshot.count=1;memset(disk.snapshot.teams[0].id,'X',sizeof(disk.snapshot.teams[0].id));load_team_cache();assert(!s_teams.count);
 puts("Team NVS: failed commits, restart, unchanged data and cross-season isolation: PASS");
}
'''
        compile_run(code)

    def test_dark_display_skips_battery_i2c(self):
        source = production_source(ROOT / 'main/main.c')
        enum_start = source.index('typedef enum {\n    BATTERY_SAMPLE_RETRY')
        enum_end = source.index('} battery_sample_outcome_t;', enum_start) + len('} battery_sample_outcome_t;')
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
'''
        code += source[enum_start:enum_end] + r'''
static bool dark;
static int lock_calls, lock_fail_on, reads, updates, last_soc;
static bool bsp_lvgl_lock(int timeout) {
 (void)timeout; lock_calls++; return lock_calls != lock_fail_on;
}
static void bsp_lvgl_unlock(void) {}
static bool pdkpass_ui_display_dark(void) {return dark;}
static int bsp_battery_soc(void) {reads++;return 55;}
static void pdkpass_ui_battery_update(int soc) {updates++;last_soc=soc;}
'''
        code += function(source, 'static battery_sample_outcome_t sample_battery_if_visible(')
        code += r'''
int main(void) {
 dark=true;
 assert(sample_battery_if_visible(true)==BATTERY_SAMPLE_PAUSE_DARK);
 assert(reads==0 && updates==0);
 dark=false;
 assert(sample_battery_if_visible(true)==BATTERY_SAMPLE_DONE);
 assert(reads==1 && updates==1 && last_soc==55);
 lock_calls=0;lock_fail_on=1;
 assert(sample_battery_if_visible(true)==BATTERY_SAMPLE_RETRY);
 assert(reads==1 && updates==1);
 lock_fail_on=0;
 assert(sample_battery_if_visible(false)==BATTERY_SAMPLE_DONE);
 assert(reads==1 && updates==2 && last_soc==-1);
 puts("dark display skips battery I2C: PASS");
}
'''
        compile_run(code)

    def test_restored_time_is_estimated_until_this_boot_syncs(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r'''
#include "pdkpass_network.h"
static bool s_in_setup, s_time_synced_boot, plausible;
static pdkpass_network_state_t s_published_state;
static const char *s_setup_error="";
static char s_setup_ssid[33], s_setup_password[16];
static pdkpass_network_update_t latest;
static void capture(const pdkpass_network_update_t *update) {latest=*update;}
static pdkpass_network_callback_t s_callback=capture;
static bool current_time_valid(void) {return plausible;}
static int64_t setup_deadline(void) {return 0;}
static int64_t esp_timer_get_time(void) {return 0;}
static void pdkpass_power_network(bool active) {(void)active;}
'''
        code += function(source, 'static void publish_state(')
        code += r'''
int main(void) {
 plausible=true;publish_state(PDKPASS_NETWORK_OFFLINE);
 assert(!latest.time_valid && latest.time_estimated);
 s_time_synced_boot=true;publish_state(PDKPASS_NETWORK_ONLINE);
 assert(latest.time_valid && !latest.time_estimated);
 plausible=false;publish_state(PDKPASS_NETWORK_OFFLINE);
 assert(!latest.time_valid && !latest.time_estimated);
 puts("restored clock estimate is not trusted for race selection: PASS");
}
'''
        compile_run(code)

    def test_offline_builtin_year_rollover(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE.replace('unsigned pdkpass_season_year(void) {return 2026;}','') + r'''
#include "pdkpass_sync_policy.h"
#define portMAX_DELAY UINT32_MAX
static bool s_time_valid;
static bool s_has_cached_data=true;
static int64_t s_last_attempt_utc;
static pdkpass_season_snapshot_t s_season;
static int callbacks, plans, transactions;
static void changed(void) {callbacks++;}
static pdkpass_season_callback_t s_callback=changed;
unsigned pdkpass_season_year(void) {return s_season.year;}
static void pdkpass_http_begin(void) {transactions++;}
static void pdkpass_http_end(void) {transactions--;}
void pdkpass_sync_plan(pdkpass_sync_service_t service,uint32_t delay) {
 assert(service==PDKPASS_SYNC_SEASON && delay==0);plans++;
}
'''
        for signature in ['static bool clock_valid(', 'static void refresh_builtin_year(',
                          'static TickType_t offline_wait(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 assert(pdkpass_calendar_load(2026,&s_season));
 refresh_builtin_year(1798732800LL);assert(s_season.year==2026&&!callbacks);
 assert(offline_wait(0,1798732799LL)==portMAX_DELAY);
 s_time_valid=true;
 assert(offline_wait(0,1798732799LL)==1000);
 assert(offline_wait(500,1798732799LL)==500);
 refresh_builtin_year(1798732799LL);assert(s_season.year==2026);
 refresh_builtin_year(1798732800LL);
 assert(s_season.year==2027&&s_season.race_count==24&&s_season.driver_count==0);
 assert(!s_has_cached_data);
 assert(callbacks==1&&plans==1&&transactions==0);
 s_season.races[0].meeting_key=999; // Accepted online data must survive.
 refresh_builtin_year(1798732801LL);
 assert(s_season.races[0].meeting_key==999&&callbacks==1);
 refresh_builtin_year(1798732799LL);assert(s_season.year==2027);
 refresh_builtin_year(1830268800LL);assert(s_season.year==2027); // No 2028 seed.
 puts("offline New Year, invalid clock, no downgrade and cache priority: PASS");
}
'''
        compile_run(code, ['main/pdkpass_calendar.c', 'main/pdkpass_data.c',
                           'main/pdkpass_tracks.c', 'main/pdkpass_season_core.c'])


    def test_relocated_bahrain_meeting_uses_sepang_venue_and_provider_identity(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE + r'''
#include <ctype.h>
typedef struct {
 const char *name,*country,*circuit,*start,*end;
 int meeting_key,circuit_key;bool cancelled;
} cJSON;
static const char *json_string(const cJSON *o,const char *name) {
 if(!strcmp(name,"meeting_name"))return o->name;
 if(!strcmp(name,"country_name"))return o->country;
 if(!strcmp(name,"circuit_short_name"))return o->circuit;
 if(!strcmp(name,"date_start"))return o->start;
 if(!strcmp(name,"date_end"))return o->end;
 return NULL;
}
static bool json_number(const cJSON *o,const char *name,int *value) {
 if(!strcmp(name,"meeting_key")){*value=o->meeting_key;return true;}
 if(!strcmp(name,"circuit_key")){*value=o->circuit_key;return true;}
 return false;
}
static const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *o,const char *name) {
 return !strcmp(name,"is_cancelled")?o:NULL;
}
static bool cJSON_IsTrue(const cJSON *o) {return o&&o->cancelled;}
'''
        start = source.index('typedef struct {\n    pdkpass_race_t race;')
        code += source[start:source.index('} race_build_t;', start)+len('} race_build_t;')] + '\n'
        start = source.index('typedef struct {\n    race_build_t *build;')
        code += source[start:source.index('} build_context_t;', start)+len('} build_context_t;')] + '\n'
        for signature in ['static void copy_text(', 'static void copy_upper(',
                          'static uint32_t accent_for_key(', 'static void apply_track_details(',
                          'static bool is_grand_prix(', 'static bool parse_meeting(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 race_build_t build[PDKPASS_MAX_RACES]={0};build_context_t ctx={.build=build};
 cJSON m={.name="Bahrain Grand Prix",.country="Bahrain",.circuit="Kuala Lumpur",
  .start="2026-10-02T04:30:00+00:00",.end="2026-10-04T09:00:00+00:00",
  .meeting_key=1308,.circuit_key=12};
 assert(parse_meeting(&m,&ctx)&&ctx.count==1);
 pdkpass_race_t *race=&build[0].race;
 assert(race->meeting_key==1308&&!strcmp(race->api_country,"Bahrain"));
 assert(!strcmp(race->country,"MALAYSIA")&&!strcmp(race->circuit,"SEPANG"));
 assert(race->circuit_length_m==5543&&race->accent==pdkpass_track_find("sepang")->accent);
 assert(!strcmp(race->weekend,"02-04 OCT"));
 // Same-image cached provider aliases normalize without rewriting query identity.
 strcpy(race->country,"BAHRAIN");strcpy(race->circuit,"KUALA LUMPUR");
 apply_track_details(race,race->circuit);
 assert(!strcmp(race->country,"MALAYSIA")&&!strcmp(race->api_country,"Bahrain"));
 assert(!strcmp(race->circuit,"SEPANG")&&race->meeting_key==1308);
 // Actual Bahrain/Sakhir, including the following season, remains Bahrain.
 m.circuit="Sakhir";m.meeting_key=1400;m.circuit_key=63;
 m.start="2027-03-12T10:00:00Z";m.end="2027-03-14T17:00:00Z";
 assert(parse_meeting(&m,&ctx)&&ctx.count==2);
 assert(!strcmp(build[1].race.country,"BAHRAIN"));
 assert(!strcmp(build[1].race.circuit,"SAKHIR")&&build[1].race.circuit_length_m==5412);
 m.cancelled=true;assert(parse_meeting(&m,&ctx)&&ctx.count==2);
 puts("Relocated GP: Sepang venue, raw API identity, cached aliases and real Bahrain: PASS");
}
'''
        compile_run(code, ['main/pdkpass_tracks.c', 'main/pdkpass_season_core.c',
                           'main/pdkpass_results_core.c'])

    def test_circuit_metadata_is_independent_of_season(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE + '\n#include <ctype.h>\n'
        for signature in ['static void copy_text(', 'static bool same_text(',
                          'static void apply_track_details(', 'static void preserve_track_details(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 pdkpass_season_snapshot_t current={.year=2026,.race_count=1};
 pdkpass_season_snapshot_t candidate={.year=2027,.race_count=3};
 strcpy(current.races[0].circuit,"MONZA");current.races[0].laps=53;
 strcpy(candidate.races[0].circuit,"Portimão");
 strcpy(candidate.races[1].circuit,"Istanbul Park");
 strcpy(candidate.races[2].circuit,"MONZA");
 preserve_track_details(&candidate,&current);
 assert(candidate.races[0].circuit_length_m==4653);
 assert(strcmp(candidate.races[0].circuit,"PORTIMAO")==0);
 assert(candidate.races[1].circuit_length_m==5338);
 assert(candidate.races[2].circuit_length_m==5793);
 assert(candidate.races[2].laps==0);
 candidate.year=2026;preserve_track_details(&candidate,&current);
 assert(candidate.races[2].laps==53);
 candidate.year=2028;candidate.races[2].laps=0;current.race_count=0;
 preserve_track_details(&candidate,&current);
 assert(candidate.races[2].circuit_length_m==5793&&candidate.races[2].laps==0);
 pdkpass_race_t unknown={.circuit_length_m=1234};
 strcpy(unknown.circuit,"UNKNOWN");apply_track_details(&unknown,unknown.circuit);
 assert(unknown.circuit_length_m==1234);
 puts("year-independent circuit metadata, aliases and event-only laps: PASS");
}
'''
        compile_run(code, ['main/pdkpass_tracks.c'])

    def test_repeated_offline_status_does_not_requeue_sync(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        code = PRELUDE + r'''
#define EVENT_WAKE 1
static int s_events=1, wakes;
static bool s_online;
static void xEventGroupSetBits(int events,int bits) {
 (void)events;assert(bits==EVENT_WAKE);wakes++;
}
'''
        code += function(source, 'void pdkpass_results_set_online(')
        code += r'''
int main(void) {
 s_lock=1;
 // Initial setup and its countdown must leave the offline worker asleep.
 for(int i=0;i<100;i++) pdkpass_results_set_online(false);
 assert(wakes==0);
 pdkpass_results_set_online(true);assert(wakes==1);
 for(int i=0;i<100;i++) pdkpass_results_set_online(true);
 assert(wakes==1);
 pdkpass_results_set_online(false);assert(wakes==2);
 // Model the feedback: offline work emits SYNC; the network publishes offline.
 int handled=1;
 while(handled<wakes && handled<10) {
  handled++;
  pdkpass_results_set_online(false);
 }
 assert(handled==2 && wakes==2);
 pdkpass_results_set_online(true);assert(wakes==3);
 puts("Offline setup feedback terminates; real transitions still wake: PASS");
}
'''
        compile_run(code)

    def test_season_worker_preserves_deadline_across_radio_parking(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE + r'''
#include <setjmp.h>
static int64_t esp_timer_get_time(void) {return 0;}
#include "pdkpass_network.h"
#include "pdkpass_sync_policy.h"
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define pdFALSE 0
#define EVENT_WAKE 1
#define SEASON_MIN_REPEAT_SECONDS 600
static int s_events, cursor, requests, transactions, synchronizations;
static int64_t now_ms, s_last_attempt_utc;
static bool online, disconnect_on_lock;
static jmp_buf done;
static pdkpass_sync_policy_t policy;
static int64_t fake_time(void *p) {(void)p;return now_ms/1000;}
#define time fake_time
static void xEventGroupWaitBits(int e,int b,int c,int a,unsigned wait) {
 (void)e;(void)b;(void)c;(void)a;(void)wait;
 static const int64_t times[]={1000000,1010000,1020000,4610000,4620000};
 static const bool states[]={false,true,false,false,true};
 if(cursor==5) longjmp(done,1);
 now_ms=times[cursor];online=states[cursor++];
}
static bool network_ready(void) {return online;}
static bool points_force_pending(void) {return false;}
static bool take_points_force(void) {return false;}
static int64_t manual_deadline(void) {assert(false);return 0;}
static bool pdkpass_http_begin_until(int64_t deadline) {(void)deadline;assert(false);return false;}
static void finish_points_force(pdkpass_manual_state_t state) {(void)state;}
static pdkpass_manual_state_t synchronize_manual_points(int64_t now) {
 (void)now;assert(false);return PDKPASS_MANUAL_FAILED;
}
static void refresh_builtin_year(int64_t now) {(void)now;}
static TickType_t offline_wait(uint32_t wait,int64_t now) {(void)now;return wait?wait:portMAX_DELAY;}
esp_err_t pdkpass_network_request(pdkpass_network_command_t c) {assert(c==PDKPASS_NETWORK_SYNC);requests++;return ESP_OK;}
uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t s) {return pdkpass_sync_policy_wait(&policy,s,now_ms);}
void pdkpass_sync_plan(pdkpass_sync_service_t s,uint32_t delay) {policy.due_ms[s]=now_ms+delay;}
static void pdkpass_http_begin(void) {transactions++;if(disconnect_on_lock) online=false;}
static void pdkpass_http_end(void) {}
static bool synchronize(int64_t now) {(void)now;synchronizations++;return true;}
void pdkpass_sync_mark_success(pdkpass_sync_status_t service,int64_t utc) {
 (void)utc;assert(service==PDKPASS_SYNC_STATUS_CALENDAR);
}
static int64_t next_sync_deadline(int64_t now) {return now+3600;}
'''
        code += function(source, 'static void season_task(')
        code += r'''
int main(void) {
 if(setjmp(done)==0) season_task(NULL);
 assert(requests==2&&transactions==2&&synchronizations==2);
 cursor=requests=transactions=synchronizations=0;s_last_attempt_utc=0;
 memset(&policy,0,sizeof(policy));disconnect_on_lock=true;
 if(setjmp(done)==0) season_task(NULL);
 assert(transactions==2&&synchronizations==0);
 puts("real season worker: offline deadlines, reconnect and shutdown race: PASS");
}
'''
        compile_run(code, ['main/pdkpass_sync_policy.c'])

    def test_sync_deadlines_and_idle_guard(self):
        compile_run(PRELUDE + r'''
#include "pdkpass_sync_policy.h"
int main(void) {
 pdkpass_sync_policy_t policy={0};
 assert(!pdkpass_sync_policy_idle(&policy,1000));
 policy.due_ms[0]=100000;policy.due_ms[1]=62000;
 assert(pdkpass_sync_policy_idle(&policy,1000));
 policy.held[PDKPASS_SYNC_SEASON]=true;assert(!pdkpass_sync_policy_idle(&policy,1000));
 policy.held[PDKPASS_SYNC_SEASON]=false;policy.held[PDKPASS_SYNC_RESULTS]=true;
 assert(!pdkpass_sync_policy_idle(&policy,1000));
 policy.held[PDKPASS_SYNC_RESULTS]=false;assert(pdkpass_sync_policy_idle(&policy,1000));
 assert(!pdkpass_sync_policy_idle(&policy,2000));
 assert(pdkpass_sync_policy_wait(&policy,PDKPASS_SYNC_RESULTS,61000)==1000);
 assert(pdkpass_sync_policy_wait(&policy,PDKPASS_SYNC_RESULTS,62000)==0);
 policy.due_ms[1]=0;assert(!pdkpass_sync_policy_idle(&policy,1000));
 policy.due_ms[1]=INT64_MAX;
 assert(pdkpass_sync_policy_wait(&policy,PDKPASS_SYNC_RESULTS,1000)==UINT32_MAX);
 puts("monotonic deadlines, busy slots and reconnect guard: PASS");
}
''', ['main/pdkpass_sync_policy.c'])

    def test_battery_startup_and_raw_measurements(self):
        source = production_source(ROOT / 'components/bsp/src/bsp_battery.c')
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "bsp_battery.h"
#include "esp_err.h"
int bsp_battery_mv(void);
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_FAIL -1
#define I2C_ADDR_BIT_LEN_7 0
#define BSP_I2C_CW2017_ADDR 0x63
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static char last_log[128];
static void capture(const char *fmt,...) {
 va_list args;va_start(args,fmt);vsnprintf(last_log,sizeof(last_log),fmt,args);va_end(args);
}
#define ESP_LOGI(tag,...) capture(__VA_ARGS__)
typedef void *i2c_master_dev_handle_t;
typedef struct {int dev_addr_length, device_address, scl_speed_hz;} i2c_device_config_t;
static i2c_master_dev_handle_t s_dev;
static uint8_t config, values[4];
static unsigned writes, delay_ms, removals, voltage_reads;
static int fail_read=-1, fail_write, read_config_calls, fail_config_read_at;
static bool ignore_writes;
static unsigned raw_soc=96*256+128, raw_mv=11840;
static int bsp_i2c_init(void) {return ESP_OK;}
static void *bsp_i2c_bus(void) {return (void *)2;}
static int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,void **out) {
 assert(bus==(void *)2 && cfg->device_address==0x63 && cfg->scl_speed_hz==100000);
 *out=(void *)1;return ESP_OK;
}
static void i2c_master_bus_rm_device(void *dev) {assert(dev==(void *)1);removals++;}
static void vTaskDelay(unsigned ticks) {assert(ticks==20 || ticks==100);delay_ms+=ticks;}
static int cw_read(uint8_t reg,uint8_t *out,size_t n) {
 if(reg==8 && ++read_config_calls==fail_config_read_at)return -1;
 if(reg==fail_read)return -1;
 if(reg==8){assert(n==1);*out=config;}
 else if(reg==0){assert(n==1);*out=0xa0;}
 else {assert(n==2 && (reg==2 || reg==4));if(reg==2)voltage_reads++;unsigned v=reg==2 ? raw_mv : raw_soc;
 out[0]=v>>8;out[1]=v&255;}
 return 0;
}
static int cw_write(uint8_t reg,uint8_t value) {
 assert(reg==8 && writes<4);values[writes++]=value;
 if((int)writes==fail_write)return -1;
 if(!ignore_writes)config=value;
 return 0;
}
'''
        code += '\n'.join(x for x in source.splitlines() if x.startswith('#define CW_REG_'))+'\n'
        for signature in ['static esp_err_t cw_ensure_active(', 'esp_err_t bsp_battery_init(',
                          'int bsp_battery_soc(', 'int bsp_battery_mv(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 config=0xf0;
 assert(bsp_battery_init()==ESP_OK && writes==2 && config==0);
 assert(values[0]==0x30 && values[1]==0 && delay_ms==120);
 assert(bsp_battery_init()==ESP_OK && writes==2);
 s_dev=NULL;writes=delay_ms=0;
 assert(bsp_battery_init()==ESP_OK && writes==0 && delay_ms==0); // retain active estimate
 for(int failure=1;failure<=2;failure++) {
  s_dev=NULL;config=0xf0;writes=0;fail_write=failure;
  assert(bsp_battery_init()==ESP_FAIL && s_dev==NULL && writes==(unsigned)failure);
 }
 fail_write=0;s_dev=NULL;fail_read=8;writes=0;
 assert(bsp_battery_init()==ESP_FAIL && !s_dev && writes==0);
 fail_read=-1;read_config_calls=0;fail_config_read_at=2;config=0xf0;writes=0;
 assert(bsp_battery_init()==ESP_FAIL && !s_dev);
 fail_config_read_at=0;ignore_writes=true;config=0xf0;writes=0;
 assert(bsp_battery_init()==ESP_FAIL && !s_dev); // readback still asleep
 ignore_writes=false;writes=0;
 assert(bsp_battery_init()==ESP_OK && s_dev);
 assert(bsp_battery_soc()==96 && voltage_reads==0);
 assert(bsp_battery_mv()==3700 && voltage_reads==1);
 raw_soc=98*256+255;assert(bsp_battery_soc()==98);
 raw_soc=99*256+255;assert(bsp_battery_soc()==99); // no synthetic full charge
 raw_soc=100*256;assert(bsp_battery_soc()==100);
 raw_soc=255*256;assert(bsp_battery_soc()==-1);
 raw_soc=96*256;fail_read=2;
 assert(bsp_battery_soc()==96 && voltage_reads==1);
 assert(bsp_battery_mv()==-1);
 fail_read=4;assert(bsp_battery_soc()==-1);
}
'''
        compile_run(code)

    def test_panel_sleep_transitions_and_failure_retry(self):
        source = production_source(ROOT / 'components/bsp/src/bsp_display.c')
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_LOGW(...) ((void)0)
static int s_panel, calls, error;
static bool s_panel_sleeping, last_sleep;
static uint8_t brightness=100;
static void bsp_display_backlight(uint8_t level) {brightness=level;}
static int esp_lcd_panel_disp_sleep(int panel,bool sleep) {
 assert(panel==1 && brightness==0); calls++;last_sleep=sleep;return error;
}
'''
        code += function(source, 'esp_err_t bsp_display_sleep(')
        code += r'''
int main(void) {
 assert(bsp_display_sleep(true)==ESP_ERR_INVALID_STATE && calls==0);
 s_panel=1;error=ESP_FAIL;
 assert(bsp_display_sleep(true)==ESP_FAIL && !s_panel_sleeping);
 assert(brightness==0 && calls==1);
 error=ESP_OK;
 assert(bsp_display_sleep(true)==ESP_OK && s_panel_sleeping && last_sleep);
 assert(bsp_display_sleep(true)==ESP_OK && calls==2);
 error=ESP_FAIL;
 assert(bsp_display_sleep(false)==ESP_FAIL && s_panel_sleeping);
 error=ESP_OK;
 assert(bsp_display_sleep(false)==ESP_OK && !s_panel_sleeping && !last_sleep);
 assert(brightness==0 && calls==4);
 assert(bsp_display_sleep(false)==ESP_OK && calls==4);
}
'''
        compile_run(code)

    def test_power_locks_and_failure_cleanup(self):
        source = production_source(ROOT / 'main/pdkpass_power.c')
        code = PRELUDE + r'''
#include "pdkpass_power.h"
typedef int *esp_pm_lock_handle_t;
typedef struct {int max_freq_mhz,min_freq_mhz;bool light_sleep_enable;} esp_pm_config_t;
#define ESP_PM_CPU_FREQ_MAX 1
#define ESP_PM_NO_LIGHT_SLEEP 2
static int counts[3], allocated, config_error;
static esp_pm_lock_handle_t s_display_cpu,s_display_awake,s_network_awake;
static bool s_display_active,s_network_active;
static int esp_pm_lock_create(int type,int arg,const char *name,esp_pm_lock_handle_t *out) {
 (void)type;(void)arg;(void)name;*out=&counts[allocated++];return ESP_OK;
}
static void esp_pm_lock_acquire(esp_pm_lock_handle_t lock) {(*lock)++;assert(*lock==1);}
static void esp_pm_lock_release(esp_pm_lock_handle_t lock) {(*lock)--;assert(*lock==0);}
static void esp_pm_lock_delete(esp_pm_lock_handle_t lock) {assert(*lock==0);}
static int esp_pm_configure(const esp_pm_config_t *c) {
 assert(c->max_freq_mhz==160 && c->min_freq_mhz==40 && c->light_sleep_enable);
 return config_error;
}
'''
        for signature in ['void pdkpass_power_display(', 'void pdkpass_power_network(',
                          'esp_err_t pdkpass_power_init(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 assert(pdkpass_power_init()==ESP_OK);assert(counts[0]==1&&counts[1]==1);
 pdkpass_power_display(true);pdkpass_power_display(false);pdkpass_power_display(false);
 assert(counts[0]==0&&counts[1]==0);
 pdkpass_power_network(true);pdkpass_power_network(true);assert(counts[2]==1);
 pdkpass_power_network(false);assert(counts[2]==0);
 allocated=0;config_error=ESP_FAIL;
 assert(pdkpass_power_init()==ESP_FAIL);
 assert(!s_display_cpu&&!s_display_awake&&!s_network_awake);
 assert(!counts[0]&&!counts[1]&&!counts[2]);
 puts("power locks, duplicate updates and error cleanup: PASS");
}
'''
        compile_run(code)

    def test_eight_character_setup_password(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r'''
static char s_setup_password[16];
static unsigned seed;
static void esp_fill_random(void *buffer,size_t length) {
 assert(length==8);
 for(size_t i=0;i<length;i++) ((uint8_t *)buffer)[i]=(uint8_t)(seed+i);
}
'''
        code += function(source, 'static void generate_setup_password(')
        code += r'''
int main(void) {
 for(seed=0;seed<256;seed++) {
  memset(s_setup_password,'!',sizeof(s_setup_password));
  generate_setup_password();assert(strlen(s_setup_password)==8);
  for(size_t i=0;i<8;i++) assert(strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789",s_setup_password[i]));
  assert(s_setup_password[9]=='!');
 }
 puts("eight-character WPA2 password generation: PASS");
}
'''
        compile_run(code)
        setup = function(source, 'static esp_err_t start_setup(')
        self.assertLess(setup.index('esp_wifi_start()'), setup.index('generate_setup_password()'))

    def test_setup_address_and_dhcp_order(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r'''
#include <arpa/inet.h>
#include "pdkpass_network.h"
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED 7
typedef struct {uint32_t addr;} esp_ip4_addr_t;
typedef struct {esp_ip4_addr_t ip,gw,netmask;} esp_netif_ip_info_t;
static int s_ap_netif=1, stage, stop_error, set_error;
static int esp_netif_str_to_ip4(const char *text,esp_ip4_addr_t *out) {
 return inet_pton(AF_INET,text,&out->addr)==1 ? ESP_OK : ESP_FAIL;
}
static int esp_netif_dhcps_stop(int netif) {assert(netif==1&&stage==0);stage=1;return stop_error;}
static int esp_netif_set_ip_info(int netif,const esp_netif_ip_info_t *info) {
 assert(netif==1&&stage==1);stage=2;
 assert(ntohl(info->ip.addr)==0xc0a80901U);assert(info->gw.addr==info->ip.addr);
 assert(ntohl(info->netmask.addr)==0xffffff00U);return set_error;
}
static int esp_netif_dhcps_start(int netif) {assert(netif==1&&stage==2);stage=3;return ESP_OK;}
'''
        code += function(source, 'static esp_err_t configure_setup_address(')
        code += r'''
int main(void) {
 assert(strcmp(PDKPASS_SETUP_IP,"192.168.9.1")==0);
 assert(configure_setup_address()==0&&stage==3);
 stage=0;stop_error=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED;
 assert(configure_setup_address()==0&&stage==3);
 stage=0;stop_error=ESP_FAIL;
 assert(configure_setup_address()==ESP_FAIL&&stage==1);
 stage=0;stop_error=0;set_error=ESP_FAIL;
 assert(configure_setup_address()==ESP_FAIL&&stage==2);
 puts("setup IP, gateway, subnet and DHCP ordering: PASS");
}
'''
        compile_run(code)
        self.assertLess(source.index('err = configure_setup_address();'),
                        source.index('err = esp_wifi_init(&wifi_init);'))

    def test_saved_scan_and_radio_shutdown(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r'''
#include "pdkpass_network.h"
#include "pdkpass_wifi_profiles.h"
#define ESP_LOGE(...) ((void)0)
#define WIFI_MODE_STA 1
#define EVENT_STOPPED 16
#define EVENT_CANDIDATE 4
#define EVENT_CLIENT 4096
#define EVENT_CANCEL 2048
#define EVENT_SETUP 1024
#define STATION_EVENTS 483
#define SAVED_CONNECT_TIMEOUT_US 15000000LL
#define pdFALSE 0
typedef unsigned EventBits_t;
typedef struct {unsigned char ssid[33];} wifi_ap_record_t;
typedef struct {bool show_hidden;struct {struct {unsigned min,max;} active;} scan_time;} wifi_scan_config_t;
static pdkpass_wifi_profiles_t s_profiles={.version=1};
static bool s_visible[5],s_radio_started,s_in_setup,s_station_active,s_associated,s_has_ip;
static int64_t s_saved_deadline,s_sync_deadline;
static unsigned s_saved_attempt;
static int s_events,scans,starts,stops,connects,closed_http,record_cursor,scan_error;
static unsigned pending;
static const char *s_setup_error;
static char s_working_ssid[33],s_working_password[65];
static const char *records[]={"Other","Second"};
static pdkpass_network_state_t state;
static int64_t esp_timer_get_time(void) {return 1000000;}
static void publish_state(pdkpass_network_state_t value) {state=value;}
static int esp_wifi_set_mode(int mode) {(void)mode;return 0;}
static int esp_wifi_start(void) {starts++;return 0;}
static int esp_wifi_stop(void) {stops++;return 0;}
static int esp_wifi_scan_start(const wifi_scan_config_t *config,bool block) {
 assert(block);assert(config->scan_time.active.max==120);scans++;record_cursor=0;return scan_error;
}
static int esp_wifi_scan_get_ap_num(uint16_t *count) {*count=2;return 0;}
static int esp_wifi_scan_get_ap_record(wifi_ap_record_t *ap) {strcpy((char *)ap->ssid,records[record_cursor++]);return 0;}
static void esp_wifi_clear_ap_list(void) {}
static unsigned xEventGroupGetBits(int event) {(void)event;return pending;}
static void xEventGroupClearBits(int event,unsigned bits) {(void)event;(void)bits;}
static unsigned xEventGroupWaitBits(int e,unsigned bits,int c,int a,unsigned t) {(void)e;(void)c;(void)a;(void)t;return bits;}
static void stop_http_server(void) {closed_http++;}
static void finish_candidate(void) {}
static int configure_station(const char *ssid,const char *password) {(void)ssid;(void)password;return 0;}
static int esp_wifi_connect(void) {connects++;return 0;}
'''
        for signature in ['static esp_err_t go_offline(', 'static esp_err_t connect_saved(',
                          'static esp_err_t scan_saved(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 assert(scan_saved()==0);assert(scans==0&&starts==0&&connects==0);
 assert(state==PDKPASS_NETWORK_OFFLINE);assert(strcmp(s_setup_error,"NO SAVED NETWORKS")==0);
 assert(pdkpass_wifi_profiles_remember(&s_profiles,"Second","test-only"));
 assert(pdkpass_wifi_profiles_remember(&s_profiles,"First","test-only"));
 assert(scan_saved()==0);assert(scans==1&&starts==1&&connects==1);
 assert(strcmp(s_working_ssid,"Second")==0);assert(!s_visible[0]&&s_visible[1]);
 assert(s_saved_attempt==2);
 assert(go_offline()==0);assert(stops==1&&!s_radio_started&&!s_in_setup);
 records[1]="Unknown";assert(scan_saved()==0);
 assert(connects==1&&!s_radio_started);assert(state==PDKPASS_NETWORK_OFFLINE);
 records[1]="Second";pending=EVENT_CANCEL;
 assert(scan_saved()==0);assert(connects==1);go_offline();
 pending=0;scan_error=ESP_FAIL;
 assert(scan_saved()==ESP_FAIL);assert(!s_radio_started);
 assert(strcmp(s_setup_error,"SCAN FAILED / RETRY")==0);
 puts("saved-only scan, absent profiles, cancellation and radio shutdown: PASS");
}
'''
        compile_run(code, ['main/pdkpass_wifi_profiles.c'])

    def test_season_legacy_calendar_load(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        defines = '\n'.join(x for x in source.splitlines()
                            if x.startswith('#define SEASON_CACHE_'))
        types = source[source.index('typedef struct {'):
                       source.index('} legacy_season_cache_t;') + len('} legacy_season_cache_t;')]
        code = PRELUDE + defines + '\n' + types + r'''
#define NVS_READONLY 0
typedef int nvs_handle_t;
static const char *NVS_NAMESPACE="test", *NVS_KEY="test";
static pdkpass_season_snapshot_t s_season;
static bool s_has_cached_data;
static season_cache_t current;
static legacy_season_cache_t legacy;
static const void *payload=&current;
static size_t payload_size=sizeof(current);
static int nvs_open(const char *n,int m,int *h) {(void)n;(void)m;*h=1;return 0;}
static int nvs_get_blob(int h,const char *k,void *out,size_t *size) {
 (void)h;(void)k;assert(*size>=payload_size);
 memcpy(out,payload,payload_size);*size=payload_size;return 0;
}
static void nvs_close(int h) {(void)h;}
'''
        for signature in ['static void copy_text(', 'static void fill_known_first_name(',
                          'static void apply_track_details(', 'static void initialize_fallback(',
                          'static bool snapshot_valid(', 'static bool restore_legacy_cache(',
                          'static void discard_bundled_standings(',
                          'static void load_cache(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 initialize_fallback();assert(s_season.race_count==23&&s_season.driver_count==0);
 assert(strcmp(s_season.standings_as_of,"PENDING")==0);
 current.magic=SEASON_CACHE_MAGIC;current.version=SEASON_CACHE_VERSION;
 current.season=s_season;current.season.race_count=11;
 memcpy(current.season.races,pdkpass_races+12,11*sizeof(pdkpass_race_t));
 current.season.driver_count=1;
 current.season.drivers[0].points_tenths=999;
 strcpy(current.season.standings_as_of,"15 SEP");
 load_cache();
 assert(s_season.race_count==23);assert(snapshot_valid(&s_season));
 assert(s_has_cached_data);
 assert(s_season.drivers[0].points_tenths==999);
 assert(strcmp(s_season.standings_as_of,"15 SEP")==0);
 current.season.year=2027;load_cache();assert(s_season.race_count==11);
 legacy.magic=SEASON_CACHE_MAGIC;
 legacy.version=SEASON_CACHE_LEGACY_VERSION;
 legacy.season.year=2026;legacy.season.race_count=11;legacy.season.driver_count=1;
 memcpy(legacy.season.races,pdkpass_races+12,11*sizeof(pdkpass_race_t));
 strcpy(legacy.season.drivers[0].code,"VER");
 strcpy(legacy.season.drivers[0].name,"VERSTAPPEN");
 legacy.season.drivers[0].points_tenths=777;
 payload=&legacy;payload_size=sizeof(legacy);load_cache();
 assert(s_has_cached_data&&s_season.race_count==23);
 assert(s_season.drivers[0].points_tenths==777);
 assert(strcmp(s_season.drivers[0].first_name,"MAX")==0);
 // Upgrade of a calendar cache containing the full old default standings.
 payload=&current;payload_size=sizeof(current);
 current.season.year=2026;current.season.driver_count=pdkpass_legacy_driver_count;
 memcpy(current.season.drivers,pdkpass_legacy_drivers,
        pdkpass_legacy_driver_count*sizeof(pdkpass_driver_t));
 strcpy(current.season.standings_as_of,"31 AUG");
 load_cache();assert(s_has_cached_data&&s_season.race_count==23);
 assert(s_season.driver_count==0&&strcmp(s_season.standings_as_of,"PENDING")==0);
 pdkpass_driver_t empty[PDKPASS_MAX_DRIVERS]={0};
 assert(memcmp(s_season.drivers,empty,sizeof(empty))==0);
 // A changed score or date must survive, even with otherwise identical drivers.
 current.season.drivers[0].points_tenths=2920;load_cache();
 assert(s_season.driver_count==pdkpass_legacy_driver_count);
 assert(s_season.drivers[0].points_tenths==2920);
 current.season.drivers[0].points_tenths=2420;
 strcpy(current.season.standings_as_of,"01 SEP");load_cache();
 assert(s_season.driver_count==pdkpass_legacy_driver_count);
 assert(strcmp(s_season.standings_as_of,"01 SEP")==0);
 // The v1 layout also drops only the recognizable bundled snapshot.
 legacy.season.driver_count=pdkpass_legacy_driver_count;
 strcpy(legacy.season.standings_as_of,"31 AUG");
 for(size_t i=0;i<pdkpass_legacy_driver_count;i++) {
  const pdkpass_driver_t *d=&pdkpass_legacy_drivers[i];
  legacy.season.drivers[i].position=d->position;
  legacy.season.drivers[i].driver_number=d->driver_number;
  legacy.season.drivers[i].points_tenths=d->points_tenths;
  memcpy(legacy.season.drivers[i].code,d->code,sizeof(d->code));
 }
 payload=&legacy;payload_size=sizeof(legacy);load_cache();
 assert(s_has_cached_data&&s_season.race_count==23&&s_season.driver_count==0);
 assert(strcmp(s_season.standings_as_of,"PENDING")==0);
 legacy.magic=0;load_cache();assert(s_season.race_count==23);
 assert(!s_has_cached_data&&s_season.driver_count==0);
 assert(strcmp(s_season.standings_as_of,"PENDING")==0);
 puts("season fallback and legacy cache load: PASS");
}
'''
        compile_run(code, ['main/pdkpass_data.c', 'main/pdkpass_tracks.c', 'main/pdkpass_calendar.c'])


    def test_results_request_during_http_overrides_later_deadline(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        code = PRELUDE + defines + r"""
#include <setjmp.h>
static int64_t esp_timer_get_time(void) {return 0;}
#include "pdkpass_sync_policy.h"
#define EVENT_WAKE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#include "pdkpass_network.h"
static jmp_buf done;
static int s_events=1;
static bool s_cache_dirty;
static size_t s_requested_race=SIZE_MAX;
static unsigned waits, processed, begins, ends, plans, wait_ms;
static void xEventGroupWaitBits(int e,int b,int c,int a,TickType_t ticks) {
 (void)e;(void)b;(void)c;(void)a;(void)ticks;
 if(waits++==2) longjmp(done,1);
}
uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t service) {
 assert(service==PDKPASS_SYNC_RESULTS);return wait_ms;
}
void pdkpass_sync_plan(pdkpass_sync_service_t service,uint32_t delay) {
 assert(service==PDKPASS_SYNC_RESULTS);wait_ms=delay;plans++;
}
static bool online_snapshot(void) {return true;}
static bool manual_race_pending(void) {return false;}
static bool pdkpass_http_expired(void) {return false;}
static bool take_manual_race(size_t *race) {(void)race;return false;}
static int64_t manual_deadline(void) {assert(false);return 0;}
static bool pdkpass_http_begin_until(int64_t deadline) {(void)deadline;assert(false);return false;}
static void finish_manual_race(size_t race,pdkpass_manual_state_t state) {
 (void)race;(void)state;assert(false);
}
static void process_manual_race(size_t race,int64_t utc) {
 (void)race;(void)utc;assert(false);
}
esp_err_t pdkpass_network_request(pdkpass_network_command_t command) {(void)command;assert(false);return ESP_OK;}
static void pdkpass_http_begin(void) {begins++;}
static void pdkpass_http_end(void) {ends++;}
static int save_cache(void) {return ESP_OK;}
static int save_cache_with_retry(void) {return save_cache();}
static TickType_t cache_retry_wait_ticks(TickType_t delay) {return delay;}
static void retry_cache_if_due(void) {}
static TickType_t next_scheduled_wait(int64_t utc) {(void)utc;return 86400000;}
static size_t select_race(int64_t utc) {
 (void)utc;
 size_t selected=s_requested_race==SIZE_MAX ? 1 : s_requested_race;
 s_requested_race=SIZE_MAX;return selected;
}
static void process_race(size_t index,int64_t utc) {
 (void)utc;
 if(processed++==0) {
  assert(index==1);
  // Model a real UI request arriving while the current round's HTTP blocks.
  s_requested_race=0;wait_ms=0;
 } else assert(index==0);
}
"""
        code += function(source, 'static bool request_pending(')
        code += function(source, 'static void results_task(')
        code += r"""
int main(void) {
 s_lock=1;
 if(setjmp(done)==0) results_task(NULL);
 assert(processed==2 && begins==2 && ends==2 && plans==2);
 puts("Manual results request survives an in-flight HTTP deadline update: PASS");
}
"""
        compile_run(code)

    def test_manual_results_refreshes_only_selected_session_and_preserves_cache(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code = PRELUDE + types + r'''
static unsigned marks[PDKPASS_SYNC_STATUS_COUNT];
void pdkpass_sync_mark_success(pdkpass_sync_status_t status,int64_t utc) {
 (void)utc;assert((unsigned)status<PDKPASS_SYNC_STATUS_COUNT);marks[status]++;
}
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static size_t s_force_race=1;
static pdkpass_manual_status_t s_force_status={.state=PDKPASS_MANUAL_RUNNING};
static bool s_cache_dirty, fail_one, expired;
static int64_t s_cache_retry_at_us;
#define RESULTS_SAVE_RETRY_MS 60000U
static const char *TAG="test";
#define portMAX_DELAY UINT32_MAX
static int64_t esp_timer_get_time(void) {return expired ? 200 : 100;}
static bool pdkpass_http_expired(void) {return expired;}
static int discoveries, fetches, saves, callbacks, cues;
static void vTaskDelay(unsigned ticks) {(void)ticks;}
static void callback(size_t race,bool first) {
 assert(race==1);assert(s_force_status.state!=PDKPASS_MANUAL_RUNNING);
 callbacks++;if(first)cues++;
}
static void (*s_callback)(size_t,bool)=callback;
static bool discover_sessions(size_t race,race_cache_t *cache,int64_t now) {
 assert(race==1);discoveries++;cache->last_discovery_utc=now;return true;
}
static bool fetch_result(session_cache_t *session) {
 fetches++;
 if(fail_one && session->session_key==22) return false;
 strcpy(session->podium_codes[0],session->session_key==11?"NEW":"FIX");
 session->ready=1;return true;
}
static int save_cache(void) {saves++;return ESP_OK;}
'''
        code += function(source, 'static esp_err_t save_cache_with_retry(')
        code += function(source, 'static void finish_manual_race(')
        code += function(source, 'static bool persisted_race_changed(')
        code += function(source, 'static void process_manual_race(')
        code += r'''
int main(void) {
 s_lock=1;races[1].meeting_key=123;s_cache[1].meeting_key=123;
 for(int i=0;i<2;i++) {
  session_cache_t *session=&s_cache[1].sessions[i];
  session->present=1;session->ready=1;session->session_key=i?22:11;
  session->end_utc=99;strcpy(session->podium_codes[0],"OLD");
 }
 s_cache[0].meeting_key=456;
 process_manual_race(1,100);
 assert(discoveries==1&&fetches==1&&saves==1&&callbacks==1&&cues==0);
 assert(s_force_status.state==PDKPASS_MANUAL_UPDATED);
 assert(strcmp(s_cache[1].sessions[0].podium_codes[0],"NEW")==0);
 assert(strcmp(s_cache[1].sessions[1].podium_codes[0],"OLD")==0);
 assert(s_cache[0].meeting_key==456&&s_cache[0].sessions[0].ready==0);
 s_force_status.state=PDKPASS_MANUAL_RUNNING;
 process_manual_race(1,101);
 assert(fetches==2&&saves==1&&s_force_status.state==PDKPASS_MANUAL_UNCHANGED);
 s_force_status.state=PDKPASS_MANUAL_RUNNING;fail_one=true;
 s_force_status.session=PDKPASS_SESSION_FP2;
 process_manual_race(1,102);
 assert(s_force_status.state==PDKPASS_MANUAL_FAILED&&saves==1);
 assert(strcmp(s_cache[1].sessions[1].podium_codes[0],"OLD")==0);
 fail_one=false;s_force_status.state=PDKPASS_MANUAL_RUNNING;
 s_force_status.session=PDKPASS_SESSION_FP1;
 for(int i=0;i<2;i++) {
  s_cache[1].sessions[i].ready=0;
  memset(s_cache[1].sessions[i].podium_codes,0,
         sizeof(s_cache[1].sessions[i].podium_codes));
 }
 process_manual_race(1,103);
 assert(saves==2&&cues==1&&s_force_status.state==PDKPASS_MANUAL_UPDATED);
 assert(!s_cache[1].sessions[1].ready);
 s_force_status.state=PDKPASS_MANUAL_RUNNING;s_force_status.deadline_us=150;expired=true;
 strcpy(s_cache[1].sessions[0].podium_codes[0],"OLD");
 process_manual_race(1,104);
 assert(s_force_status.state==PDKPASS_MANUAL_TIMED_OUT&&saves==2);
 assert(strcmp(s_cache[1].sessions[0].podium_codes[0],"OLD")==0);
 assert(marks[PDKPASS_SYNC_STATUS_RESULTS]==3);
 assert(!marks[PDKPASS_SYNC_STATUS_CALENDAR]&&!marks[PDKPASS_SYNC_STATUS_DRIVERS]);
 puts("Manual results: selected session, corrections, unchanged, timeout and failure cache: PASS");
}
'''
        compile_run(code)

    def test_background_result_completes_matching_queued_manual_refresh(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        code = PRELUDE + defines + '\n' + types + r'''
#define portMAX_DELAY UINT32_MAX
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static bool s_cache_dirty, s_force_pending, fetch_ok=true, save_fail;
static size_t s_force_race, requested_race=1;
static unsigned requested_session=PDKPASS_SESSION_FP1;
static pdkpass_manual_status_t s_force_status;
static int64_t clock_us=100;
static int fetches, callbacks;
static pdkpass_manual_state_t observed_state;
static bool observed_hold;
static int64_t esp_timer_get_time(void) {return clock_us;}
static bool discovery_due(const race_cache_t *cache,int64_t now) {
 (void)cache;(void)now;return false;
}
static bool discover_sessions(size_t race,race_cache_t *cache,int64_t now) {
 (void)race;(void)cache;(void)now;assert(false);return false;
}
static int64_t retry_interval_seconds(size_t race,int64_t now) {
 (void)race;(void)now;return 600;
}
static void queue_manual(void) {
 s_force_pending=true;s_force_race=requested_race;
 s_force_status=(pdkpass_manual_status_t){.state=PDKPASS_MANUAL_RUNNING,
  .session=requested_session,.generation=2,.deadline_us=1000};
 pdkpass_sync_hold(PDKPASS_SYNC_RESULTS,true);
}
static bool fetch_result(session_cache_t *session) {
 fetches++;queue_manual(); // OK is pressed while the automatic HTTP is active.
 if(!fetch_ok)return false;
 session->ready=1;strcpy(session->podium_codes[0],"NEW");return true;
}
static int save_cache_with_retry(void) {
 s_cache_dirty=save_fail;return save_fail?ESP_FAIL:ESP_OK;
}
void pdkpass_sync_mark_success(pdkpass_sync_status_t status,int64_t utc) {
 assert(status==PDKPASS_SYNC_STATUS_RESULTS);(void)utc;
}
static void callback(size_t race,bool first) {
 assert(race==1);(void)first;callbacks++;
 observed_state=s_force_status.state;observed_hold=sync_held[PDKPASS_SYNC_RESULTS];
}
static void (*s_callback)(size_t,bool)=callback;
static void reset(void) {
 memset(s_cache,0,sizeof(s_cache));s_cache_dirty=false;s_force_pending=false;
 memset(&s_force_status,0,sizeof(s_force_status));
 s_cache[1].sessions[0]=(session_cache_t){.present=1,.session_key=11,.end_utc=6000};
 fetch_ok=true;save_fail=false;requested_race=1;requested_session=PDKPASS_SESSION_FP1;
 clock_us=100;fetches=callbacks=0;sync_held[PDKPASS_SYNC_RESULTS]=false;
}
'''
        for signature in ['static void finish_manual_race(',
                          'static void finish_pending_manual_result(',
                          'static process_outcome_t process_race(',
                          'static bool take_manual_race(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 s_lock=1;size_t next;
 reset();process_race(1,10000);
 assert(s_cache[1].sessions[0].ready&&fetches==1&&callbacks==1);
 assert(s_force_status.state==PDKPASS_MANUAL_UPDATED&&s_force_status.generation==3);
 assert(observed_state==PDKPASS_MANUAL_UPDATED&&!observed_hold);
 assert(!take_manual_race(&next)&&!sync_held[PDKPASS_SYNC_RESULTS]);

 reset();requested_session=PDKPASS_SESSION_FP2;process_race(1,10000);
 assert(s_force_status.state==PDKPASS_MANUAL_RUNNING&&observed_hold);
 assert(take_manual_race(&next)&&next==1); // A different session still needs HTTP.
 reset();requested_race=0;process_race(1,10000);
 assert(s_force_status.state==PDKPASS_MANUAL_RUNNING&&observed_hold);
 assert(take_manual_race(&next)&&next==0); // A different round stays queued.

 reset();save_fail=true;process_race(1,10000);
 assert(s_cache_dirty&&s_force_status.state==PDKPASS_MANUAL_RUNNING&&observed_hold);
 assert(take_manual_race(&next)); // An unsaved result cannot complete the request.
 reset();fetch_ok=false;process_race(1,10000);
 assert(!s_cache[1].sessions[0].ready&&s_force_status.state==PDKPASS_MANUAL_RUNNING);
 assert(sync_held[PDKPASS_SYNC_RESULTS]&&take_manual_race(&next));

 reset();clock_us=1001;process_race(1,10000);
 assert(s_force_status.state==PDKPASS_MANUAL_TIMED_OUT);
 assert(observed_state==PDKPASS_MANUAL_TIMED_OUT&&!observed_hold);
 assert(!take_manual_race(&next)); // Preserve the original deadline.

 reset();s_cache[1].sessions[0].ready=1;queue_manual();process_race(1,10000);
 assert(fetches==0&&s_force_status.state==PDKPASS_MANUAL_RUNNING);
 assert(sync_held[PDKPASS_SYNC_RESULTS]&&take_manual_race(&next));
 // Existing cached results still require a manual check for corrections.
 puts("Automatic result: coalesced matching manual request, deadline and failure ownership: PASS");
}
'''
        compile_run(code, ['main/pdkpass_results_core.c'])

    def test_manual_points_refreshes_only_selected_table_without_calendar(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE.replace('static void pdkpass_http_release(void) {}\n', '') + r'''
static unsigned marks[PDKPASS_SYNC_STATUS_COUNT];
void pdkpass_sync_mark_success(pdkpass_sync_status_t status,int64_t utc) {
 (void)utc;assert((unsigned)status<PDKPASS_SYNC_STATUS_COUNT);marks[status]++;
}
static pdkpass_points_target_t s_points_force_target;
static pdkpass_season_snapshot_t s_season;
static pdkpass_team_snapshot_t s_teams;
static bool s_has_cached_data, team_fails, expired, unchanged, driver_save_fails;
static bool pdkpass_http_expired(void) {return expired;}
static int driver_fetches,team_fetches,driver_saves,team_saves,callbacks;
static void callback(void) {callbacks++;}
static void (*s_callback)(void)=callback;
unsigned pdkpass_beijing_year(int64_t now) {(void)now;return 2026;}
bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *out) {*out=s_season;return true;}
bool pdkpass_season_team_snapshot(pdkpass_team_snapshot_t *out) {*out=s_teams;return true;}
static bool tls_released=true, publication_alloc_fails;
static size_t network_working_bytes;
static void pdkpass_http_release(void) {tls_released=true;}
static void *points_calloc(size_t count,size_t size) {
 network_working_bytes=count*size;
 assert(network_working_bytes<=2048);return calloc(count,size);
}
static void *points_malloc(size_t size) {
 assert(tls_released);return publication_alloc_fails?NULL:malloc(size);
}
#define calloc points_calloc
#define malloc points_malloc
static bool fetch_driver_standings(int64_t now,unsigned year,const char *previous,
 pdkpass_driver_t *drivers,uint8_t *count,char *as_of) {
 (void)now;assert(year==2026);tls_released=false;driver_fetches++;
 assert(network_working_bytes<=2048);
 memcpy(drivers,s_season.drivers,sizeof(s_season.drivers));
 drivers[0].points_tenths+=unchanged?0:10;*count=s_season.driver_count;
 memcpy(as_of,previous,12);return true;
}
static bool fetch_team_standings(int64_t now,pdkpass_team_snapshot_t *out) {
 (void)now;team_fetches++;
 if(team_fails)return false;
 out->teams[0].points_tenths+=unchanged?0:10;return true;
}
static int save_cache(const pdkpass_season_snapshot_t *value) {
 (void)value;driver_saves++;return driver_save_fails?ESP_FAIL:ESP_OK;
}
static int save_team_cache(const pdkpass_team_snapshot_t *value) {
 (void)value;team_saves++;return ESP_OK;
}
'''
        code += function(source, 'static pdkpass_manual_state_t synchronize_manual_drivers(')
        code += function(source, 'static pdkpass_manual_state_t synchronize_manual_teams(')
        code += function(source, 'static pdkpass_manual_state_t synchronize_manual_points(')
        code += r'''
int main(void) {
 s_lock=1;s_season.year=2026;s_teams.year=2026;
 s_season.driver_count=1;s_teams.count=1;
 s_season.drivers[0].points_tenths=100;s_teams.teams[0].points_tenths=100;
 // Driver page must not request or modify the team table, even if it would fail.
 team_fails=true;s_points_force_target=PDKPASS_POINTS_DRIVERS;
 pdkpass_team_snapshot_t old_teams=s_teams;
 assert(synchronize_manual_points(100)==PDKPASS_MANUAL_UPDATED);
 assert(driver_fetches==1&&team_fetches==0&&driver_saves==1&&team_saves==0);
 assert(s_season.drivers[0].points_tenths==110&&!memcmp(&old_teams,&s_teams,sizeof(old_teams)));
 assert(callbacks==1&&s_has_cached_data);
 assert(marks[PDKPASS_SYNC_STATUS_DRIVERS]==1&&!marks[PDKPASS_SYNC_STATUS_TEAMS]);
 // Team page must neither request nor modify the driver/calendar snapshot.
 s_points_force_target=PDKPASS_POINTS_TEAMS;
 pdkpass_season_snapshot_t old=s_season;
 assert(synchronize_manual_points(101)==PDKPASS_MANUAL_FAILED);
 assert(driver_fetches==1&&team_fetches==1&&callbacks==1);
 team_fails=false;
 assert(synchronize_manual_points(102)==PDKPASS_MANUAL_UPDATED);
 assert(team_fetches==2&&team_saves==1&&callbacks==2);
 assert(!memcmp(&old,&s_season,sizeof(old))&&s_teams.teams[0].points_tenths==110);
 assert(marks[PDKPASS_SYNC_STATUS_DRIVERS]==1&&marks[PDKPASS_SYNC_STATUS_TEAMS]==1);
 expired=true;
 assert(synchronize_manual_points(103)==PDKPASS_MANUAL_TIMED_OUT);
 assert(team_saves==1&&callbacks==2);
 s_points_force_target=PDKPASS_POINTS_DRIVERS;
 assert(synchronize_manual_points(104)==PDKPASS_MANUAL_TIMED_OUT);
 assert(driver_saves==1&&team_saves==1&&callbacks==2);
 expired=false;unchanged=true;
 assert(synchronize_manual_points(105)==PDKPASS_MANUAL_UNCHANGED);
 assert(marks[PDKPASS_SYNC_STATUS_DRIVERS]==2&&marks[PDKPASS_SYNC_STATUS_TEAMS]==1);
 s_points_force_target=PDKPASS_POINTS_TEAMS;
 assert(synchronize_manual_points(106)==PDKPASS_MANUAL_UNCHANGED);
 assert(marks[PDKPASS_SYNC_STATUS_DRIVERS]==2&&marks[PDKPASS_SYNC_STATUS_TEAMS]==2);
 unchanged=false;s_points_force_target=PDKPASS_POINTS_DRIVERS;driver_save_fails=true;
 assert(synchronize_manual_points(107)==PDKPASS_MANUAL_FAILED);
 assert(!memcmp(&old,&s_season,sizeof(old)));
 driver_save_fails=false;publication_alloc_fails=true;
 assert(synchronize_manual_points(108)==PDKPASS_MANUAL_FAILED);
 assert(!memcmp(&old,&s_season,sizeof(old)));
 assert(marks[PDKPASS_SYNC_STATUS_DRIVERS]==2&&marks[PDKPASS_SYNC_STATUS_TEAMS]==2);
 assert(!marks[PDKPASS_SYNC_STATUS_CALENDAR]);
 puts("Manual points: selected table only, independent dates, atomic failures and deadlines: PASS");
}
'''
        compile_run(code)

    def test_results_scheduling(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code = PRELUDE + defines + '\n' + types + r'''
#include "pdkpass_sync_policy.h"
#define EVENT_WAKE 1
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static size_t s_requested_race = SIZE_MAX, s_priority_race = SIZE_MAX, s_history_cursor;
static int64_t s_priority_until_utc;
static int s_events=1, sync_plan_calls, wake_calls;
static time_t fake_now;
static time_t fake_time(time_t *out) {(void)out;return fake_now;}
#define time fake_time
static void xEventGroupSetBits(int events,int bits) {(void)events;(void)bits;wake_calls++;}
void pdkpass_sync_plan(pdkpass_sync_service_t service,uint32_t delay) {
 assert(service==PDKPASS_SYNC_RESULTS && delay==0);sync_plan_calls++;
}
static bool s_cache_dirty, s_force_pending;
static size_t s_force_race = SIZE_MAX;
static pdkpass_manual_status_t s_force_status;
static int64_t s_cache_retry_at_us;
#define portMAX_DELAY UINT32_MAX
static int64_t esp_timer_get_time(void) {return (int64_t)fake_now*1000000;}
static const char *TAG="test";
static pdkpass_results_callback_t s_callback;
static bool discover_ok, fetch_ok, save_fail;
static int discover_calls, fetch_calls, marks, result_cues;
static void on_result(size_t race_index, bool new_result) {
 assert(race_index<race_count);
 if(new_result)result_cues++;
}
static bool discover_sessions(size_t i, race_cache_t *cache, int64_t now) {
 (void)i; (void)cache; (void)now; discover_calls++; return discover_ok;
}
static bool fetch_result(session_cache_t *session) {
 fetch_calls++; if (fetch_ok) session->ready=1; return fetch_ok;
}
static esp_err_t save_cache(void) {return save_fail ? ESP_FAIL : ESP_OK;}
void pdkpass_sync_mark_success(pdkpass_sync_status_t service,int64_t utc) {
 (void)utc;assert(service==PDKPASS_SYNC_STATUS_RESULTS);marks++;
}
'''
        code = code.replace('static pdkpass_results_callback_t s_callback;',
                            'static void (*s_callback)(size_t,bool);')
        for signature in ['static esp_err_t save_cache_with_retry(', 'static int64_t retry_interval_seconds(', 'static bool cache_has_due_result(',
                          'static bool discovery_due(', 'static bool cache_complete(',
                          'static bool race_is_eligible(', 'static bool race_needs_work(',
                          'static void expedite_requested_race(',
                          'static size_t select_race(', 'static void finish_manual_race(',
                          'static void finish_pending_manual_result(',
                          'static process_outcome_t process_race(',
                          'static TickType_t next_scheduled_wait(',
                          'void pdkpass_results_request_race(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 int64_t now = 1788688800LL;
 s_lock=1;
 s_callback=on_result;
 fake_now=now;
 races[0].switch_at_utc=now-10*86400; races[0].meeting_key=10;
 races[1].switch_at_utc=now+3600; races[1].meeting_key=20;
 s_cache[0].meeting_key=10; s_cache[1].meeting_key=20;
 assert(select_race(now)==1); // Current weekend wins over historical discovery.
 s_cache[1].next_discovery_utc=now+21600;
 s_cache[1].sessions[0]=(session_cache_t){.present=1,.session_key=200,.end_utc=now-1790};
 assert(select_race(now)==0);
 process_race(0,now); // A failed historical discovery has its own deadline.
 assert(discover_calls==1);
 assert(!race_needs_work(0,now+1));
 assert(next_scheduled_wait(now)==10000); // Current FP1 becomes due in ten seconds.
 assert(select_race(now+10)==1);
 process_race(1,now+10);
 assert(fetch_calls==1);
 assert(marks==0 && result_cues==0); // Discovery and failed fetch stay silent.
 assert(!race_needs_work(1,now+11));
 assert(race_needs_work(1,now+611));
 fetch_ok=true; process_race(1,now+611);
 assert(marks==1 && result_cues==1);
 s_cache[1].discovered=1;
 s_cache[1].next_discovery_utc=now+21600;
 s_cache[1].sessions[1]=(session_cache_t){.present=1,.session_key=201,
  .end_utc=now-4000};
 save_fail=true;process_race(1,now+612);
 assert(s_cache[1].sessions[1].ready);
 assert(marks==1 && result_cues==1); // A failed save must not play a cue.
 save_fail=false;
 assert(s_cache[0].next_discovery_utc==now+86400);
 fake_now=now+RESULTS_MANUAL_RETRY_SECONDS+1;
 pdkpass_results_request_race(0);
 assert(select_race(fake_now)==0);
 assert(s_cache[0].next_discovery_utc==fake_now);
 assert(sync_plan_calls==1 && wake_calls==1);
 s_cache[0].discovered=1;
 s_cache[0].next_discovery_utc=fake_now+86400;
 s_cache[0].sessions[PDKPASS_SESSION_FP1]=(session_cache_t){
  .present=1,.session_key=123,.end_utc=fake_now-10000,
  .last_attempt_utc=fake_now-RESULTS_MANUAL_RETRY_SECONDS};
 pdkpass_results_request_race(0);
 assert(select_race(fake_now)==0);
 assert(s_cache[0].sessions[PDKPASS_SESSION_FP1].last_attempt_utc==0);

 // A requested history race must stay ahead of the current weekend until
 // its available podiums are filled, not just for a single HTTP transaction.
 memset(s_cache,0,sizeof(s_cache));
 s_cache_dirty=false;fetch_ok=true;s_requested_race=SIZE_MAX;
 races[0].switch_at_utc=fake_now-10*86400;
 races[1].switch_at_utc=fake_now+3600;
 for(size_t i=0;i<2;i++) {
  s_cache[i].meeting_key=races[i].meeting_key;
  s_cache[i].discovered=1;s_cache[i].next_discovery_utc=fake_now+21600;
  s_cache[i].last_discovery_utc=fake_now;
  for(size_t j=0;j<PDKPASS_SESSION_COUNT;j++) {
   s_cache[i].sessions[j]=(session_cache_t){.present=1,
    .session_key=(int)(100+i*10+j),.end_utc=fake_now-4000};
  }
 }
 pdkpass_results_request_race(0);
 assert(select_race(fake_now)==0);
 process_race(0,fake_now);
 assert(s_cache[0].sessions[0].ready);
 assert(select_race(fake_now+5)==0);
 for(size_t j=1;j<PDKPASS_SESSION_COUNT;j++) {
  assert(select_race(fake_now+5*(int64_t)j)==0);
  process_race(0,fake_now+5*(int64_t)j);
 }
 assert(select_race(fake_now+100)==1); // Completed focus no longer blocks others.
 for(size_t j=0;j<PDKPASS_SESSION_COUNT;j++) s_cache[0].sessions[j].ready=0;

 // An immediate selection after a failed history request schedules its retry
 // in five minutes, rather than silently waiting for a daily retry.
 s_cache[0].discovered=0;
 s_cache[0].last_discovery_utc=fake_now;
 s_cache[0].next_discovery_utc=fake_now+86400;
 for(size_t j=0;j<PDKPASS_SESSION_COUNT;j++)
  s_cache[0].sessions[j].last_attempt_utc=fake_now;
 pdkpass_results_request_race(0);
 select_race(fake_now+1);
 assert(s_cache[0].next_discovery_utc==fake_now+RESULTS_MANUAL_RETRY_SECONDS);
 assert(retry_interval_seconds(0,fake_now+1)==RESULTS_MANUAL_RETRY_SECONDS);
 // Choosing another round replaces the focus; unrelated history stays slow.
 pdkpass_results_request_race(1);
 assert(select_race(fake_now+2)==1);
 assert(retry_interval_seconds(0,fake_now+2)==86400);

 // No new interaction: focused historical retry expires, but current
 // weekend automatic results still retry every ten minutes.
 pdkpass_results_request_race(0);
 select_race(fake_now+3);
 assert(retry_interval_seconds(0,fake_now+3+RESULTS_PRIORITY_SECONDS-1)==300);
 assert(retry_interval_seconds(0,fake_now+3+RESULTS_PRIORITY_SECONDS)==86400);
 assert(retry_interval_seconds(1,fake_now+3+RESULTS_PRIORITY_SECONDS)==600);
 select_race(fake_now+3+RESULTS_PRIORITY_SECONDS);
 assert(s_priority_race==SIZE_MAX);
 pdkpass_results_request_race(0);select_race(fake_now+4+RESULTS_PRIORITY_SECONDS);
 assert(retry_interval_seconds(0,fake_now+4+RESULTS_PRIORITY_SECONDS)==300);

 puts("service results scheduling: PASS");
}
'''
        compile_run(code, ['main/pdkpass_results_core.c'])

    def test_partial_season_does_not_replace_cache(self):
        source = production_source(ROOT / 'main/pdkpass_season.c')
        code = PRELUDE.replace('static void pdkpass_http_release(void) {}\n', '') + r'''
#define ESP_ERR_NO_MEM 0x101
typedef struct {pdkpass_race_t race;int64_t start_utc,meeting_end_utc;} race_build_t;
typedef struct {race_build_t *build;size_t count;bool sprint_qualifying[PDKPASS_MAX_RACES];} build_context_t;
static void copy_text(char *d,size_t n,const char *s) {snprintf(d,n,"%s",s);}
static bool parse_meeting(const void *item,void *user) {(void)item;(void)user;return true;}
static bool populate_session(const void *item,void *user) {(void)item;(void)user;return true;}
static int compare_races(const void *a,const void *b) {(void)a;(void)b;return 0;}
static bool sessions_fail, tls_released, publication_alloc_fails;
static size_t calendar_working_bytes;
static void pdkpass_http_release(void) {tls_released=true;}
static void *calendar_calloc(size_t count,size_t size) {
 if(size==sizeof(pdkpass_season_snapshot_t)) {
  assert(tls_released);if(publication_alloc_fails)return NULL;
 } else {assert(!calendar_working_bytes);calendar_working_bytes=count*size;}
 return calloc(count,size);
}
#define calloc calendar_calloc
static esp_err_t pdkpass_http_array(const char *url,bool (*item)(const void *,void *),void *user) {
 (void)item;build_context_t *ctx=user;
 assert(calendar_working_bytes<=6144);tls_released=false;
 if(strstr(url,"/meetings?")) {
  ctx->count=1;ctx->build[0].race=races[0];
  strcpy(ctx->build[0].race.race_cn,"RACE SCHEDULE TBD");
  return ESP_OK;
 }
 return sessions_fail ? ESP_FAIL : ESP_OK;
}
static void preserve_track_details(pdkpass_season_snapshot_t *a,const pdkpass_season_snapshot_t *b) {(void)a;(void)b;}
static void pdkpass_http_report_data_failure(const char *stage,esp_err_t err,size_t bytes) {
 (void)stage;(void)err;(void)bytes;
}
'''
        code += function(source, 'static bool snapshot_valid(')
        code += function(source, 'static bool build_candidate(')
        code += r'''
int main(void) {
 pdkpass_season_snapshot_t current={.year=2026,.race_count=1}, *candidate=NULL;
 races[0].meeting_key=10;races[0].round=1;races[0].switch_at_utc=1788688800;
 current.races[0]=races[0];strcpy(current.races[0].race_cn,"RACE 06 SEP 16:00");
 current.driver_count=1;current.drivers[0].points_tenths=2420;
 strcpy(current.standings_as_of,"31 AUG");
 sessions_fail=true;
 assert(!build_candidate(2026,&current,&candidate));
 assert(!candidate);calendar_working_bytes=0;
 sessions_fail=false;publication_alloc_fails=true;
 assert(!build_candidate(2026,&current,&candidate)&&!candidate);
 calendar_working_bytes=0;publication_alloc_fails=false;
 assert(build_candidate(2026,&current,&candidate));
 assert(strcmp(candidate->races[0].race_cn,current.races[0].race_cn)==0);
 // Calendar refresh retains standings; synchronize updates them independently.
 assert(candidate->drivers[0].points_tenths==2420);
 assert(strcmp(candidate->standings_as_of,"31 AUG")==0);
 free(candidate);
 puts("Calendar: compact GET working set, deferred snapshot and atomic failure: PASS");
}
'''
        compile_run(code, ['main/pdkpass_season_core.c'])

    def test_cache_migration_and_identity(self):
        source = production_source(ROOT / 'main/pdkpass_results.c')
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code = PRELUDE + defines + '\n' + types + r'''
#define NVS_READONLY 0
#define NVS_READWRITE 1
static const char *NVS_NAMESPACE="test", *NVS_KEY="season";
typedef int nvs_handle_t;
static const void *payload;
static size_t payload_size;
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static unsigned s_cache_year;
static size_t s_cache_count, s_requested_race, s_priority_race;
static int64_t s_priority_until_utc;
static bool s_cache_dirty;
static int64_t s_cache_retry_at_us;
static bool s_has_cached_data;
static int nvs_open(const char *name,int mode,int *handle) {(void)name;(void)mode;*handle=1;return 0;}
static int nvs_get_blob(int h,const char *key,void *out,size_t *size) {
 (void)h;(void)key; assert(*size>=payload_size);memcpy(out,payload,payload_size);*size=payload_size;return 0;
}
static void nvs_close(int h) {(void)h;}
static int nvs_erase_all(int h) {(void)h;return 0;}
static int nvs_commit(int h) {(void)h;return 0;}
'''
        for signature in ['static void reset_race_cache(', 'static bool persisted_matches(',
                          'static int stored_index(', 'static void load_cache(',
                          'static void update_session_identity(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 s_lock=1;races[0].meeting_key=10;races[1].meeting_key=20;
 legacy_store_t legacy={.magic=RESULTS_CACHE_MAGIC,.version=2,.year=2026,.race_count=2};
 legacy.races[0].meeting_key=10;legacy.races[1].meeting_key=20;
 legacy.races[0].present_mask=1;legacy.races[0].ready_mask=1;
 memcpy(legacy.races[0].podium_codes[0][0],"AAA",4);
 payload=&legacy;payload_size=sizeof(legacy);load_cache();
 assert(s_cache[0].sessions[0].ready);
 assert(s_has_cached_data);
 update_session_identity(&s_cache[0].sessions[0],100,false,1788688800);
 assert(s_cache[0].sessions[0].ready);
 assert(strcmp(s_cache[0].sessions[0].podium_codes[0],"AAA")==0);
 update_session_identity(&s_cache[0].sessions[0],100,false,1788689800);
 assert(s_cache[0].sessions[0].ready);
 update_session_identity(&s_cache[0].sessions[0],101,false,1788689800);
 assert(!s_cache[0].sessions[0].ready);
 results_store_t stored={.magic=RESULTS_CACHE_MAGIC,.version=RESULTS_CACHE_VERSION,.year=2026,.race_count=2};
 stored.races[0].meeting_key=20;stored.races[0].session_keys[0]=200;
 stored.races[0].ready_mask=1;stored.races[0].present_mask=1;
 stored.races[1].meeting_key=10;stored.races[1].session_keys[0]=100;
 stored.races[1].ready_mask=1;stored.races[1].present_mask=1;
 payload=&stored;payload_size=sizeof(stored);load_cache();
 assert(s_cache[0].sessions[0].session_key==100);
 assert(s_cache[1].sessions[0].session_key==200);
 assert(s_cache[0].sessions[0].ready);
 update_session_identity(&s_cache[0].sessions[0],100,true,1788689800);
 assert(!s_cache[0].sessions[0].ready);
 memset(&stored.races,0,sizeof(stored.races));
 stored.race_count=11;race_count=23;
 for (size_t i=0;i<23;i++) races[i].meeting_key=i<12 ? (int)i+1000 : 0;
 stored.races[0].ready_mask=1;stored.races[0].present_mask=1;
 stored.races[0].session_keys[0]=321;
 load_cache();
 assert(s_cache[12].sessions[0].ready);
 assert(s_cache[12].sessions[0].session_key==321);
 assert(!s_cache[0].sessions[0].ready);
 assert(!s_cache[13].sessions[0].ready);
 assert(s_has_cached_data);
 memset(&stored.races,0,sizeof(stored.races));
 stored.race_count=23;payload=&stored;payload_size=sizeof(stored);load_cache();
 assert(!s_has_cached_data);
 puts("cache v2 migration, v3 identity, meeting reorder and R13 shift: PASS");
}
'''
        compile_run(code)

    def test_sync_menu_distinguishes_legacy_cache_from_never_synced(self):
        source = production_source(ROOT / 'main/pdkpass_ui.c')
        code = '#define _POSIX_C_SOURCE 200809L\n' + PRELUDE + r'''
#include "pdkpass_sync_policy.h"
#define BEIJING_OFFSET_SECONDS 28800LL
'''
        code += function(source, 'static void format_sync_line(')
        code += r'''
int main(void) {
 char line[32];
 format_sync_line(PDKPASS_SYNC_STATUS_CALENDAR,2026,0,false,line,sizeof(line));
 assert(strcmp(line,"CAL SYNC NEVER")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_CALENDAR,2026,0,true,line,sizeof(line));
 assert(strcmp(line,"CAL CACHE DATE?")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_RESULTS,2026,0,true,line,sizeof(line));
 assert(strcmp(line,"RESULT CACHE DATE?")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_RESULTS,2026,0,false,line,sizeof(line));
 assert(strcmp(line,"RESULTS NEVER")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_CALENDAR,2026,1767225600LL,true,line,sizeof(line));
 assert(strcmp(line,"CAL SYNC 26.01.01")==0);
 // The UTC boundary at 16:00 is Beijing New Year, not the preceding season.
 format_sync_line(PDKPASS_SYNC_STATUS_CALENDAR,2027,1798732799LL,false,line,sizeof(line));
 assert(strcmp(line,"CAL 2027 NO SYNC")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_RESULTS,2027,1798732799LL,false,line,sizeof(line));
 assert(strcmp(line,"RESULT 2027 NO SYNC")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_RESULTS,2027,1798732799LL,true,line,sizeof(line));
 assert(strcmp(line,"RESULT CACHE DATE?")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_CALENDAR,2027,1798732800LL,false,line,sizeof(line));
 assert(strcmp(line,"CAL SYNC 27.01.01")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_DRIVERS,2026,1767225600LL,true,line,sizeof(line));
 assert(strcmp(line,"DRIVERS 26.01.01")==0);
 format_sync_line(PDKPASS_SYNC_STATUS_TEAMS,2026,0,true,line,sizeof(line));
 assert(strcmp(line,"TEAMS CACHE DATE?")==0);
 puts("sync menu distinguishes old cache, no cache and dated sync: PASS");
}
'''
        compile_run(code)

    def test_battery_warning_has_contrasting_background(self):
        source = production_source(ROOT / 'main/pdkpass_ui.c')
        code = PRELUDE + r'''
#define UI_RED 0xE53935
#define UI_PAPER 0xFFF9E8
#define LV_OPA_COVER 255
#define LV_OPA_TRANSP 0
#define LV_OBJ_FLAG_HIDDEN 1
typedef struct {uint32_t bg, border; int opa, width; bool hidden;} lv_obj_t;
static lv_obj_t body, level, tip;
static lv_obj_t *s_battery=&body, *s_battery_fill=&level, *s_battery_tip=&tip;
static int s_battery_soc;
static uint32_t s_status_background;
static uint32_t s_battery_background=UINT32_MAX;
static uint32_t lv_color_hex(uint32_t x) {return x;}
static uint32_t readable_text_color(uint32_t x) {(void)x;return 0xFFFFFF;}
static void lv_obj_set_style_bg_color(lv_obj_t *o,uint32_t x,int sel) {(void)sel;o->bg=x;}
static void lv_obj_set_style_border_color(lv_obj_t *o,uint32_t x,int sel) {(void)sel;o->border=x;}
static void lv_obj_set_style_bg_opa(lv_obj_t *o,int x,int sel) {(void)sel;o->opa=x;}
static void lv_obj_set_width(lv_obj_t *o,int x) {o->width=x;}
static void lv_obj_add_flag(lv_obj_t *o,int x) {(void)x;o->hidden=true;}
static void lv_obj_remove_flag(lv_obj_t *o,int x) {(void)x;o->hidden=false;}
static void lv_obj_invalidate(lv_obj_t *o) {(void)o;}
'''
        code += function(source, 'void pdkpass_ui_battery_update(')
        code += r'''
int main(void) {
 s_status_background=UI_RED;
 for(int soc=0;soc<=20;soc++) {
  pdkpass_ui_battery_update(soc);
  assert(body.border!=s_status_background && tip.bg!=s_status_background);
  assert(body.opa==LV_OPA_COVER && body.bg!=UI_RED);
  assert(level.bg==UI_RED && level.hidden==(soc==0));
 }
 pdkpass_ui_battery_update(21);assert(body.opa==LV_OPA_TRANSP);
 assert(level.bg!=UI_RED);
 pdkpass_ui_battery_update(-1);assert(body.opa==LV_OPA_TRANSP && level.hidden);
 puts("Battery warning contrast and recovery: PASS");
}
'''
        compile_run(code)

    def test_wifi_profile_persistence_without_legacy_import(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r'''
#include "pdkpass_wifi_profiles.h"
#define NVS_NAMESPACE "test"
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_INVALID_ARG 2
typedef int nvs_handle_t;
static pdkpass_wifi_profiles_t s_profiles={.version=1}, persisted, pending;
static char s_working_ssid[33], s_working_password[65];
static bool have_blob, fail_commit, fail_open, short_blob;
static unsigned closes;
static int nvs_open(const char *name,int mode,nvs_handle_t *handle) {
 assert(!strcmp(name,NVS_NAMESPACE));(void)mode;*handle=1;
 return fail_open?ESP_FAIL:ESP_OK;
}
static void nvs_close(nvs_handle_t handle) {assert(handle==1);closes++;}
static int nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *size) {
 assert(handle==1&&!strcmp(key,"profiles"));if(!have_blob)return ESP_FAIL;
 assert(*size>=sizeof(persisted));memcpy(out,&persisted,sizeof(persisted));
 *size=sizeof(persisted)-(short_blob?1:0);return ESP_OK;
}
static int nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t size) {
 assert(handle==1&&!strcmp(key,"profiles")&&size==sizeof(pending));
 memcpy(&pending,data,size);return ESP_OK;
}
static int nvs_commit(nvs_handle_t handle) {
 assert(handle==1);if(fail_commit)return ESP_FAIL;
 persisted=pending;have_blob=true;return ESP_OK;
}
'''
        code += function(source, 'static bool load_credentials(')
        code += function(source, 'static esp_err_t save_credentials(')
        code += r'''
int main(void) {
 strcpy(s_working_ssid,"stale");strcpy(s_working_password,"stale");
 s_profiles.count=1;fail_open=true;
 assert(!load_credentials()&&s_profiles.version==1&&!s_profiles.count&&!closes);
 assert(!s_working_ssid[0]&&!s_working_password[0]);
 fail_open=false;assert(!load_credentials()&&!s_profiles.count&&closes==1);
 assert(save_credentials("First","test-only")==ESP_OK&&have_blob);
 assert(save_credentials("Second","test-only")==ESP_OK);
 pdkpass_wifi_profiles_t before=s_profiles;
 fail_commit=true;assert(save_credentials("Third","test-only")==ESP_FAIL);
 assert(memcmp(&before,&s_profiles,sizeof(before))==0);
 memset(&s_profiles,0,sizeof(s_profiles));assert(load_credentials());
 assert(s_profiles.count==2&&strcmp(s_working_ssid,"Second")==0);
 persisted.count=0;assert(!load_credentials()&&!s_profiles.count);
 assert(!s_working_ssid[0]&&!s_working_password[0]);
 persisted.count=6;assert(!load_credentials()&&s_profiles.version==1&&!s_profiles.count);
 persisted=before;persisted.version=2;
 assert(!load_credentials()&&s_profiles.version==1&&!s_profiles.count);
 persisted=before;short_blob=true;
 assert(!load_credentials()&&s_profiles.version==1&&!s_profiles.count);
 assert(!s_working_ssid[0]&&!s_working_password[0]);
 short_blob=false;fail_commit=false;
 assert(save_credentials("New","test-only")==ESP_OK&&s_profiles.count==1);
 assert(load_credentials()&&strcmp(s_working_ssid,"New")==0);
 puts("Wi-Fi profiles reload, invalid data reset and failed commit preservation: PASS");
}
'''
        compile_run(code, ['main/pdkpass_wifi_profiles.c'])

    def test_provisioning_uses_one_immutable_attempt(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r'''
#include "pdkpass_network.h"
#include "pdkpass_wifi_form.h"
#define FORM_BODY_LIMIT 320
#define portMAX_DELAY 0xffffffffU
#define pdFALSE 0
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_WIFI_NOT_CONNECT 2
#define ESP_ERR_INVALID_STATE 3
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define EVENT_CONNECTED 1
#define EVENT_DISCONNECTED 2
#define EVENT_CANDIDATE 4
#define EVENT_STOPPED 16
#define EVENT_ASSOCIATED 32
#define STATION_EVENTS (EVENT_CONNECTED | EVENT_DISCONNECTED | EVENT_ASSOCIATED)
#define WIFI_MODE_STA 1
typedef unsigned EventBits_t;
typedef struct {unsigned content_len;const char *body;} httpd_req_t;
typedef struct {unsigned char ssid[33];} wifi_ap_record_t;
static int s_candidate_lock, s_events;
static bool s_candidate_busy, s_testing_candidate, s_has_ip, s_in_setup=true;
static bool s_have_working_credentials;
static bool s_station_active, s_associated;
static const char *s_setup_error;
static int64_t s_sync_deadline;
static int stop_calls, start_calls, connect_calls;
static bool stop_event=true;
static int64_t s_candidate_deadline, s_saved_deadline, s_saved_retry_at;
#define SAVED_RETRY_INTERVAL_US 60000000LL
static char s_candidate_ssid[33],s_candidate_password[65],s_attempt_ssid[33],s_attempt_password[65];
static char s_working_ssid[33],s_working_password[65],saved_ssid[33],saved_password[65];
static char connected_ssid[33];
static int response_status;
static const char _binary_pdkpass_setup_status_html_start[] = "";
static void httpd_resp_set_status(httpd_req_t *r,const char *status) {(void)r;response_status=atoi(status);}
static void httpd_resp_set_type(httpd_req_t *r,const char *type) {(void)r;(void)type;}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value) {(void)r;(void)name;(void)value;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *body) {(void)r;(void)body;return 0;}
static void httpd_resp_send_err(httpd_req_t *r,int status,const char *body) {(void)r;(void)body;response_status=status;}
static int httpd_req_recv(httpd_req_t *r,char *out,unsigned length) {memcpy(out,r->body,length);return (int)length;}
static int xEventGroupSetBits(int event,unsigned bits) {(void)event;return bits;}
static int xEventGroupClearBits(int event,unsigned bits) {(void)event;return bits;}
static EventBits_t xEventGroupWaitBits(int event,unsigned bits,int clear,int all,unsigned wait) {
 (void)event;(void)clear;(void)all;(void)wait;return stop_event ? bits : 0;
}
static int esp_wifi_stop(void) {stop_calls++;return ESP_OK;}
static int esp_wifi_start(void) {start_calls++;return ESP_OK;}
static int esp_wifi_connect(void) {connect_calls++;return ESP_OK;}
static int esp_wifi_set_mode(int mode) {(void)mode;return ESP_OK;}
static int64_t esp_timer_get_time(void) {return 1000000;}
static void publish_state(pdkpass_network_state_t state) {(void)state;}
static int configure_station(const char *ssid,const char *password) {(void)password;strcpy(connected_ssid,ssid);return 0;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {strcpy((char *)ap->ssid,connected_ssid);return 0;}
static int save_credentials(const char *ssid,const char *password) {strcpy(saved_ssid,ssid);strcpy(saved_password,password);return 0;}
static void stop_http_server(void) {}
'''
        for signature in ['static void finish_candidate(', 'static esp_err_t save_post(',
                          'static esp_err_t disconnect_station(', 'static esp_err_t test_candidate(',
                          'static esp_err_t accept_candidate(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 const char *a="ssid=TestA&password=abcdefgh";
 const char *b="ssid=TestB&password=ijklmnop";
 httpd_req_t req={.body=a,.content_len=(unsigned)strlen(a)};
 assert(save_post(&req)==ESP_OK);assert(response_status==202);
 req.body=b;req.content_len=(unsigned)strlen(b);
 assert(save_post(&req)==ESP_OK);assert(response_status==409);
 assert(strcmp(s_candidate_ssid,"TestA")==0);
 stop_event=false; // First boot: no old link, so no stop/disconnect event.
 assert(test_candidate()==ESP_OK);assert(s_testing_candidate);
 assert(stop_calls==0);assert(connect_calls==1);
 strcpy(connected_ssid,"WrongAP");
 assert(accept_candidate()==ESP_ERR_INVALID_STATE);assert(!s_have_working_credentials);
 strcpy(connected_ssid,"TestA");
 // Even if the pending slot were corrupted, commit the actual in-flight copy.
 strcpy(s_candidate_ssid,"TestB");strcpy(s_candidate_password,"ijklmnop");
 assert(accept_candidate()==ESP_OK);
 assert(strcmp(saved_ssid,"TestA")==0);assert(strcmp(saved_password,"abcdefgh")==0);
 assert(!s_candidate_busy);assert(!s_testing_candidate);assert(s_attempt_password[0]==0);
 // Switch away from an active link/scan: stop barrier precedes new connection.
 stop_event=true;
 assert(save_post(&req)==ESP_OK);assert(response_status==202);
 assert(test_candidate()==ESP_OK);
 assert(stop_calls==1);assert(start_calls==1);assert(connect_calls==2);
 assert(strcmp(s_attempt_ssid,"TestB")==0);
 finish_candidate();
 // A missing stop barrier must never allow a new configuration/connection.
 stop_event=false;
 assert(test_candidate()==ESP_ERR_TIMEOUT);assert(connect_calls==2);
 finish_candidate();
 // A failed association has already returned the station to idle; retry works.
 s_station_active=false;
 assert(test_candidate()==ESP_OK);assert(connect_calls==3);
 finish_candidate();
 puts("provisioning duplicate rejection and immutable commit: PASS");
}
'''
        compile_run(code, ['main/pdkpass_wifi_form.c'])

    def test_reconnect_does_not_require_a_second_ntp_success(self):
        source = production_source(ROOT / 'main/pdkpass_network.c')
        code = PRELUDE + r'''
#include <setjmp.h>
#include "pdkpass_network.h"
#include "pdkpass_wifi_profiles.h"
#define ESP_LOGE(...) ((void)0)
#define EVENT_CONNECTED 1
#define EVENT_DISCONNECTED 2
#define EVENT_CANDIDATE 4
#define EVENT_TIME_SYNCED 8
#define EVENT_SYNC 8192
#define EVENT_POLICY 16384
#define EVENT_RETRY 512
#define EVENT_SETUP 1024
#define EVENT_CANCEL 2048
#define EVENT_CLIENT 4096
#define EVENT_ASSOCIATED 32
#define EVENT_AUTH_ERROR 64
#define EVENT_AP_MISSING 128
#define EVENT_SECURITY_ERROR 256
#define STATION_EVENTS (EVENT_CONNECTED | EVENT_DISCONNECTED | EVENT_ASSOCIATED | EVENT_AUTH_ERROR | EVENT_AP_MISSING | EVENT_SECURITY_ERROR)
#define pdFALSE 0
#define SAVED_RETRY_INTERVAL_US 60000000LL
#define BACKGROUND_RETRY_INTERVAL_US 300000000LL
#define SAVED_CONNECT_TIMEOUT_US 15000000LL
#define portMAX_DELAY UINT32_MAX
#define WIFI_MODE_STA 1
static jmp_buf finished;
typedef unsigned EventBits_t;
typedef struct {unsigned char ssid[33];} wifi_ap_record_t;
static int s_events;
static bool s_time_synced_boot, s_has_ip, s_in_setup, s_testing_candidate;
static bool s_have_working_credentials=true;
static bool s_station_active, s_associated;
static const char *s_setup_error;
static bool s_visible[5]={true,true,true,true,true};
static int64_t s_setup_started, s_setup_idle_since;
static pdkpass_network_state_t s_published_state;
static int64_t s_candidate_deadline, s_sync_deadline, now_us;
static int64_t s_idle_check;
static bool s_auto_parked, sync_idle, http_available=true;
static bool s_background_retry_active;
static int64_t s_background_retry_at;
bool pdkpass_sync_idle(void) {return sync_idle;}
static bool pdkpass_http_try_begin(void) {return http_available;}
static void pdkpass_http_end(void) {}
static int64_t s_saved_deadline, s_saved_retry_at;
static unsigned s_saved_attempt;
static pdkpass_wifi_profiles_t s_profiles = {.version=1};
static char s_working_ssid[33], s_working_password[65], connected_ssid[33];
typedef struct {int num;} wifi_sta_list_t;
static int phone_count, connection_count, setup_count;
static int online_count, time_error_count, sync_count, cursor, event_count;
static unsigned script[8];
static int64_t times[8];
static unsigned xEventGroupWaitBits(int e,unsigned bits,int clear,int all,unsigned timeout) {
 (void)e;(void)bits;(void)clear;(void)all;(void)timeout;
 if(cursor>=event_count) longjmp(finished,1);
 now_us=times[cursor];return script[cursor++];
}
static int connect_saved(void);
static int scan_calls, offline_count;
static int go_offline(void) {
 offline_count++;s_in_setup=false;s_saved_deadline=0;s_has_ip=false;s_testing_candidate=false;return ESP_OK;
}
static int scan_failures;
static int scan_saved(void) {scan_calls++;s_saved_attempt=0;if(scan_failures>0){scan_failures--;return go_offline();}return connect_saved();}
static int prepare_failures, prepare_calls;
static int prepare_network(void) {prepare_calls++;if(prepare_failures>0){prepare_failures--;return ESP_FAIL;}return connect_saved();}
static void vTaskDelete(void *task) {(void)task;}
static int test_candidate(void) {
 s_testing_candidate=true;s_saved_deadline=0;s_candidate_deadline=now_us+30000000LL;return ESP_OK;
}
static void finish_candidate(void) {s_testing_candidate=false;}
static int start_setup(void) {s_in_setup=true;s_saved_deadline=0;s_setup_started=now_us;s_setup_idle_since=now_us;setup_count++;return ESP_OK;}
static int esp_wifi_connect(void) {connection_count++;return ESP_OK;}
static int configure_station(const char *ssid,const char *password) {(void)password;strcpy(connected_ssid,ssid);return ESP_OK;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {strcpy((char *)ap->ssid,connected_ssid);return ESP_OK;}
static int esp_wifi_ap_get_sta_list(wifi_sta_list_t *clients) {clients->num=phone_count;return ESP_OK;}
static int esp_wifi_set_mode(int mode) {(void)mode;return ESP_OK;}
static void stop_http_server(void) {}
static int save_credentials(const char *ssid,const char *password) {return pdkpass_wifi_profiles_remember(&s_profiles,ssid,password)?ESP_OK:1;}
static int accept_candidate(void) {return ESP_OK;}
static int disconnect_station(void) {return ESP_OK;}
static bool current_time_valid(void) {return true;}
static void save_current_time(void) {}
static void start_sntp_once(void) {sync_count++;}
static int64_t esp_timer_get_time(void) {return now_us;}
static void publish_state(pdkpass_network_state_t state) {
 if(state==PDKPASS_NETWORK_ONLINE) online_count++;
 if(state==PDKPASS_NETWORK_TIME_ERROR) time_error_count++;
}
'''
        code += function(source, 'static int64_t setup_deadline(')
        code += function(source, 'static esp_err_t connect_saved(')
        code += function(source, 'static void plan_background_retry(')
        code += function(source, 'static TickType_t network_wait_ticks(')
        code += function(source, 'static void network_task(')
        code += r'''
int main(void) {
 // Initial failure stays alive, ignores automatic SYNC and retries only user actions.
 prepare_failures=2;prepare_calls=0;event_count=3;cursor=0;
 script[0]=EVENT_SYNC;script[1]=EVENT_RETRY;script[2]=EVENT_SETUP;
 if(setjmp(finished)==0) network_task(NULL);
 assert(prepare_calls==3&&setup_count==1&&s_in_setup);
 s_in_setup=false;s_has_ip=false;s_saved_deadline=0;s_testing_candidate=false;
 s_candidate_deadline=s_sync_deadline=s_idle_check=0;
 setup_count=offline_count=scan_calls=connection_count=0;cursor=event_count=0;
 // Deadline selection must not poll an idle connection or miss a pending form.
 assert(network_wait_ticks(0)==portMAX_DELAY);
 s_saved_deadline=15000000;assert(network_wait_ticks(0)==15000);
 s_testing_candidate=true;s_candidate_deadline=10000000;
 assert(network_wait_ticks(0)==10000);
 assert(network_wait_ticks(10000000)==1);
 s_saved_deadline=0;s_testing_candidate=false;
 s_has_ip=true;s_sync_deadline=60000000;
 assert(network_wait_ticks(0)==60000);
 s_has_ip=false;s_sync_deadline=0;
 assert(pdkpass_wifi_profiles_remember(&s_profiles,"TestA","test-only"));
 s_in_setup=true;s_saved_retry_at=60000000;
 assert(network_wait_ticks(0)==1000);
 s_testing_candidate=true;s_candidate_deadline=30000000;
 assert(network_wait_ticks(0)==1000);
 s_testing_candidate=false;s_in_setup=false;
 // A plausible NVS time alone cannot authorize a new HTTPS season sync.
 script[0]=EVENT_CONNECTED;times[0]=1;
 script[1]=0;times[1]=61000001;event_count=2;
 if(setjmp(finished)==0) network_task(NULL);
 assert(online_count==0);assert(time_error_count==1);
 cursor=0;event_count=5;online_count=0;time_error_count=0;
 s_time_synced_boot=false;s_has_ip=false;s_sync_deadline=0;
 script[0]=EVENT_CONNECTED;times[0]=1;
 script[1]=EVENT_TIME_SYNCED;times[1]=2;
 script[2]=EVENT_DISCONNECTED;times[2]=3;
 script[3]=EVENT_CONNECTED;times[3]=4;
 script[4]=0;times[4]=61000005;
 if(setjmp(finished)==0) network_task(NULL);
 assert(online_count==2);assert(time_error_count==0);assert(s_time_synced_boot);
 // Exhausted saved networks power down; there is no periodic background retry.
 assert(pdkpass_wifi_profiles_remember(&s_profiles,"TestB","test-only"));
 cursor=0;event_count=4;now_us=0;s_saved_attempt=0;
 s_sync_deadline=0;s_has_ip=false;connection_count=0;
 for(int i=0;i<4;i++){script[i]=EVENT_DISCONNECTED;times[i]=i+1;}
 if(setjmp(finished)==0) network_task(NULL);
 assert(connection_count==4);assert(setup_count==0);assert(!s_in_setup);
 assert(strcmp(connected_ssid,"TestA")==0);
 cursor=0;event_count=1;script[0]=0;times[0]=60000005;phone_count=1;
 if(setjmp(finished)==0) network_task(NULL);
 assert(connection_count==4);
 cursor=0;times[0]=120000006;phone_count=0;
 if(setjmp(finished)==0) network_task(NULL);
 assert(connection_count==4);assert(setup_count==0);
 // Only an explicit command opens setup; no clients expires at three minutes.
 cursor=0;event_count=2;script[0]=EVENT_SETUP;times[0]=1000;
 script[1]=0;times[1]=180001000;phone_count=0;
 if(setjmp(finished)==0) network_task(NULL);
 assert(!s_in_setup);assert(setup_count==1);
 // Attached phones cannot extend the absolute ten-minute cap.
 cursor=0;script[0]=EVENT_SETUP;times[0]=200000000;
 script[1]=0;times[1]=800000000;phone_count=1;
 if(setjmp(finished)==0) network_task(NULL);
 assert(!s_in_setup);assert(setup_count==2);
 // Real worker exposes actionable failure messages without saving candidates.
 pdkpass_wifi_profiles_t before=s_profiles;
 cursor=0;event_count=3;script[0]=EVENT_SETUP;times[0]=199000000;
 script[1]=EVENT_CANDIDATE;times[1]=200000000;
 script[2]=EVENT_DISCONNECTED|EVENT_AUTH_ERROR;times[2]=200000001;
 if(setjmp(finished)==0) network_task(NULL);
 assert(!s_testing_candidate);assert(strcmp(s_setup_error,"AUTH FAILED / RETRY")==0);
 assert(memcmp(&before,&s_profiles,sizeof(before))==0);
 cursor=0;script[2]=EVENT_DISCONNECTED|EVENT_AP_MISSING;
 if(setjmp(finished)==0) network_task(NULL);
 assert(strcmp(s_setup_error,"WIFI NOT FOUND")==0);
 cursor=0;event_count=4;script[2]=EVENT_ASSOCIATED;script[3]=0;times[3]=230000001;
 if(setjmp(finished)==0) network_task(NULL);
 assert(strcmp(s_setup_error,"IP ADDRESS TIMEOUT")==0);
 assert(!s_testing_candidate);assert(memcmp(&before,&s_profiles,sizeof(before))==0);
 // Idle deadline restarts after a phone leaves, but never extends the cap.
 s_setup_started=1000000;s_setup_idle_since=1000000;s_testing_candidate=false;
 assert(setup_deadline()==181000000);
 s_setup_idle_since=-1;assert(setup_deadline()==601000000);
 s_setup_idle_since=550000000;assert(setup_deadline()==601000000);
 s_setup_idle_since=1000000;s_testing_candidate=true;
 assert(setup_deadline()==601000000);
 // Quiescent services may park a synchronized link, then a due service wakes it.
 s_in_setup=false;s_testing_candidate=false;s_has_ip=false;s_saved_attempt=0;
 sync_idle=true;http_available=true;s_time_synced_boot=true;s_auto_parked=false;
 cursor=0;event_count=3;now_us=0;
 int before_scan=scan_calls;
 script[0]=EVENT_CONNECTED;times[0]=1;
 script[1]=EVENT_POLICY;times[1]=31000001;
 script[2]=EVENT_SYNC;times[2]=32000001;
 if(setjmp(finished)==0) network_task(NULL);
 assert(scan_calls==before_scan+1);assert(!s_auto_parked);
 // A failed/offline link is never revived by an automatic service request.
 s_saved_attempt=4;s_has_ip=false;s_auto_parked=false;
 cursor=0;event_count=1;script[0]=EVENT_SYNC;times[0]=33000001;
 before_scan=scan_calls;
 if(setjmp(finished)==0) network_task(NULL);
 assert(scan_calls==before_scan);
 // A missed saved network on scheduled wake must retry with the display dark.
 // Two failed attempts park the radio between checks; no UI/network command
 // is needed for the third scan to reconnect and release waiting services.
 s_saved_attempt=4;s_has_ip=false;s_in_setup=false;s_auto_parked=true;
 s_background_retry_active=false;s_background_retry_at=0;scan_failures=2;
 s_idle_check=s_sync_deadline=s_saved_deadline=0;
 before_scan=scan_calls;cursor=0;event_count=5;
 script[0]=EVENT_SYNC;times[0]=172800000000LL; // two days of powered standby
 script[1]=EVENT_POLICY;times[1]=172801000000LL; // unrelated event cannot renew deadline
 script[2]=0;times[2]=173100000000LL;
 script[3]=0;times[3]=173400000000LL;
 script[4]=EVENT_CONNECTED;times[4]=173400000001LL;
 if(setjmp(finished)==0) network_task(NULL);
 assert(scan_calls==before_scan+3&&s_has_ip);
 assert(!s_background_retry_active&&!s_background_retry_at);
 // Losing a previously synchronized working connection has the same recovery.
 s_saved_attempt=4;s_has_ip=true;s_in_setup=false;s_auto_parked=false;
 s_saved_deadline=0;s_idle_check=0;scan_failures=1;
 before_scan=scan_calls;cursor=0;event_count=3;
 script[0]=EVENT_DISCONNECTED;times[0]=700000000;
 script[1]=0;times[1]=1000000000;
 script[2]=EVENT_CONNECTED;times[2]=1000000001;
 if(setjmp(finished)==0) network_task(NULL);
 assert(scan_calls==before_scan+2&&s_has_ip&&!s_background_retry_at);
 // Explicit cancellation clears both the power-saving wake and pending retry.
 s_saved_attempt=4;s_has_ip=false;s_auto_parked=false;
 s_saved_deadline=s_idle_check=0;s_background_retry_active=true;
 s_background_retry_at=1300000000;
 assert(network_wait_ticks(1000000000)==300000);
 cursor=0;event_count=3;before_scan=scan_calls;
 script[0]=EVENT_CANCEL;times[0]=1000000001;
 script[1]=EVENT_SYNC;times[1]=1000000002;
 script[2]=0;times[2]=1300000000;
 if(setjmp(finished)==0) network_task(NULL);
 assert(scan_calls==before_scan&&!s_background_retry_active&&!s_background_retry_at);
 // Entering setup also cancels the pending background retry.
 s_saved_attempt=4;s_has_ip=false;s_auto_parked=false;
 s_background_retry_active=true;s_background_retry_at=1400000000;
 cursor=0;event_count=2;before_scan=scan_calls;
 script[0]=EVENT_SETUP;times[0]=1300000001;
 script[1]=EVENT_CANCEL;times[1]=1300000002;
 if(setjmp(finished)==0) network_task(NULL);
 assert(scan_calls==before_scan&&!s_background_retry_active&&!s_background_retry_at);
 // An active HTTP transaction must prevent radio shutdown.
 s_saved_attempt=0;http_available=false;s_has_ip=false;now_us=0;
 cursor=0;event_count=2;script[0]=EVENT_CONNECTED;times[0]=1;
 script[1]=EVENT_POLICY;times[1]=31000001;
 if(setjmp(finished)==0) network_task(NULL);
 assert(s_has_ip&&!s_auto_parked);
 // Manual cancel disarms the previously scheduled reconnect.
 s_has_ip=false;s_auto_parked=true;s_saved_attempt=4;
 cursor=0;event_count=2;script[0]=EVENT_CANCEL;times[0]=32000001;
 script[1]=EVENT_SYNC;times[1]=33000001;before_scan=scan_calls;
 if(setjmp(finished)==0) network_task(NULL);
 assert(!s_auto_parked&&scan_calls==before_scan);
 // An unreachable initial NTP server cannot keep the radio awake forever.
 s_has_ip=false;s_in_setup=false;s_saved_attempt=0;s_time_synced_boot=false;now_us=0;
 cursor=0;event_count=2;script[0]=EVENT_CONNECTED;times[0]=1;
 script[1]=0;times[1]=60000001;
 if(setjmp(finished)==0) network_task(NULL);
 assert(!s_has_ip&&!s_auto_parked);
 assert(strcmp(s_setup_error,"TIME SYNC FAILED")==0);
 puts("NTP, bounded setup, scheduled parking and manual override: PASS");
}
'''
        compile_run(code, ['main/pdkpass_wifi_profiles.c'])

    def test_offline_manual_jobs_connect_once_then_run_or_timeout(self):
        for service in ('season', 'results'):
            source = production_source(ROOT / f'main/pdkpass_{service}.c')
            code = PRELUDE + r'''
#include <setjmp.h>
#include "pdkpass_network.h"
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define EVENT_WAKE 1
#define RESULTS_IDLE_DELAY_MS 86400000U
#define RESULTS_BACKFILL_DELAY_MS 5000U
#define SEASON_MIN_REPEAT_SECONDS 300
static jmp_buf done;
static int s_events=1;
static bool pending,online,s_cache_dirty,fail_connect_request;
static unsigned cursor,retries,begins,ends,finishes,processed,mode;
static int64_t now_us,s_last_attempt_utc;
static pdkpass_manual_state_t terminal;
static int64_t esp_timer_get_time(void) {return now_us;}
static void xEventGroupWaitBits(int e,int bits,int clear,int all,unsigned wait) {
 (void)e;(void)bits;(void)clear;(void)all;
 if(cursor==3)longjmp(done,1);
 if(cursor==1)assert(wait==1000); // Keep request pending and poll without busy-spinning.
 now_us=cursor*1000000LL;
 online=mode==0&&cursor==2;
 if(mode==1&&cursor==2)now_us=120000000LL;
 cursor++;
}
static bool points_force_pending(void) {return pending;}
static bool manual_race_pending(void) {return pending;}
static bool network_ready(void) {return online;}
static bool online_snapshot(void) {return online;}
static bool request_pending(void) {return false;}
static int64_t manual_deadline(void) {return 120000000LL;}
static bool take_points_force(void) {bool value=pending;pending=false;return value;}
static bool take_manual_race(size_t *race) {*race=0;return take_points_force();}
static void finish_points_force(pdkpass_manual_state_t state) {finishes++;terminal=state;}
static void finish_manual_race(size_t race,pdkpass_manual_state_t state) {assert(race==0);finish_points_force(state);}
esp_err_t pdkpass_network_request(pdkpass_network_command_t command) {
 if(command==PDKPASS_NETWORK_RETRY) {retries++;return fail_connect_request?ESP_FAIL:ESP_OK;}
 assert(command==PDKPASS_NETWORK_SYNC);return ESP_OK;
}
uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t service) {(void)service;return 3600000;}
void pdkpass_sync_plan(pdkpass_sync_service_t service,uint32_t wait) {(void)service;(void)wait;}
static bool pdkpass_http_begin_until(int64_t deadline) {assert(online&&deadline==120000000);begins++;return true;}
static void pdkpass_http_begin(void) {assert(false);}
static void pdkpass_http_end(void) {ends++;}
static bool pdkpass_http_expired(void) {return false;}
static pdkpass_manual_state_t synchronize_manual_points(int64_t now) {(void)now;processed++;return PDKPASS_MANUAL_UPDATED;}
static void process_manual_race(size_t race,int64_t now) {assert(race==0);(void)now;processed++;finish_points_force(PDKPASS_MANUAL_UPDATED);}
static void refresh_builtin_year(int64_t now) {(void)now;}
static TickType_t offline_wait(uint32_t wait,int64_t now) {(void)now;return wait?wait:portMAX_DELAY;}
static bool synchronize(int64_t now) {(void)now;assert(false);return false;}
static int64_t next_sync_deadline(int64_t now) {return now+3600;}
static TickType_t cache_retry_wait_ticks(TickType_t wait) {return wait;}
static void retry_cache_if_due(void) {}
static int save_cache_with_retry(void) {assert(false);return ESP_FAIL;}
static TickType_t next_scheduled_wait(int64_t now) {(void)now;return 3600000;}
static size_t select_race(int64_t now) {(void)now;assert(false);return SIZE_MAX;}
static void process_race(size_t race,int64_t now) {(void)race;(void)now;assert(false);}
'''
            code += function(source, f'static void {service}_task(')
            code += f'''
int main(void) {{
 pending=true;mode=0;
 if(setjmp(done)==0){service}_task(NULL);
 assert(retries==1&&begins==1&&ends==1&&processed==1&&finishes==1);
 assert(!pending&&terminal==PDKPASS_MANUAL_UPDATED);
 cursor=retries=begins=ends=processed=finishes=0;pending=true;online=false;mode=1;
 if(setjmp(done)==0){service}_task(NULL);
 assert(retries==1&&begins==0&&ends==0&&finishes==1&&!pending);
 assert(terminal==PDKPASS_MANUAL_TIMED_OUT);
 cursor=retries=begins=ends=processed=finishes=0;pending=true;online=false;mode=2;
 fail_connect_request=true;
 if(setjmp(done)==0){service}_task(NULL);
 assert(retries==1&&begins==0&&ends==0&&finishes==1&&!pending);
 assert(terminal==PDKPASS_MANUAL_OFFLINE);
 puts("Offline manual {service}: connect once, preserve queued work, run after IP, timeout and allocation failure: PASS");
}}
'''
            # An immediate failure uses the background wait, rather than a connection poll.
            code = code.replace('if(cursor==1)assert(wait==1000);',
                                'if(cursor==1&&!fail_connect_request)assert(wait==1000);')
            compile_run(code)

    def test_manual_hold_blocks_parking_without_overwriting_background_deadline(self):
        source = production_source(ROOT / 'main/pdkpass_sync.c')
        code = r'''
#include <assert.h>
#include <stdio.h>
#include "pdkpass_sync_policy.h"
#include "pdkpass_network.h"
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int s_guard;
static pdkpass_sync_policy_t s_policy;
static unsigned notifications;
static int64_t esp_timer_get_time(void) {return 1000000;}
esp_err_t pdkpass_network_request(pdkpass_network_command_t command) {
 assert(command==PDKPASS_NETWORK_POLICY);notifications++;return ESP_OK;
}
'''
        code += function(source, 'void pdkpass_sync_hold(')
        code += function(source, 'bool pdkpass_sync_idle(')
        code += r'''
int main(void) {
 s_policy.due_ms[0]=s_policy.due_ms[1]=3600000;
 assert(pdkpass_sync_idle());
 pdkpass_sync_hold(PDKPASS_SYNC_SEASON,true);assert(!pdkpass_sync_idle());
 pdkpass_sync_hold(PDKPASS_SYNC_RESULTS,true);
 pdkpass_sync_hold(PDKPASS_SYNC_SEASON,false);assert(!pdkpass_sync_idle());
 pdkpass_sync_hold(PDKPASS_SYNC_RESULTS,false);assert(pdkpass_sync_idle());
 assert(s_policy.due_ms[0]==3600000&&s_policy.due_ms[1]==3600000);
 assert(notifications==4);
 pdkpass_sync_hold(PDKPASS_SYNC_COUNT,true);assert(notifications==4);
 puts("Accepted work holds radio across connecting/queueing, preserves schedule, releases each owner: PASS");
}
'''
        compile_run(code, ['main/pdkpass_sync_policy.c'])


    def test_upgrades_preserve_storage_and_isolate_partition_recovery(self):
        source = production_source(ROOT / 'main/pdkpass_cache.c')
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pdkpass_cache.h"
typedef int nvs_handle_t;
typedef struct {uint8_t app_elf_sha256[32];} esp_app_desc_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 9
#define ESP_ERR_NVS_INVALID_LENGTH 10
#define ESP_ERR_NVS_TYPE_MISMATCH 11
#define ESP_ERR_NVS_NO_FREE_PAGES 12
#define ESP_ERR_NVS_NEW_VERSION_FOUND 13
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_LOGW(...) ((void)0)
static const char *TAG="test";
static bool s_ready;
static esp_app_desc_t image={.app_elf_sha256={2}};
static const esp_app_desc_t *esp_app_get_description(void) {return &image;}
static const char *const names[]={"pdk_season","pdk_results","pdk_sync","pdk_reminder","pdkpass_net","pdk_meta","unknown"};
static uint8_t disk[3][7][64],staged[3][7][64],recovery[32];
static size_t sizes[3][7],staged_sizes[3][7];
static unsigned step,fail_step,erases[2];
static int init_error,init_error_store,marker_error;
static bool partial_erase;
static int operation(void) {return ++step==fail_step?ESP_FAIL:ESP_OK;}
static int partition_index(const char *name) {
 if(!strcmp(name,"nvs"))return 0;
 assert(!strcmp(name,PDKPASS_CACHE_PARTITION));return 1; // No access to cardid/Recovery.
}
static int namespace_index(const char *name) {
 for(int i=0;i<7;i++)if(!strcmp(name,names[i]))return i;
 assert(false);return -1;
}
static int nvs_flash_init_partition(const char *partition) {
 int store=partition_index(partition);int err=operation();if(err!=ESP_OK)return err;
 if(store==init_error_store&&init_error){err=init_error;init_error=0;return err;}
 return ESP_OK;
}
static int nvs_flash_erase_partition(const char *partition) {
 int store=partition_index(partition);assert(!s_ready);
 int err=operation();if(err!=ESP_OK)return err;
 erases[store]++;
 memset(disk[store],0,sizeof(disk[store]));memset(sizes[store],0,sizeof(sizes[store]));
 if(partial_erase){partial_erase=false;return ESP_FAIL;}
 return ESP_OK;
}
static int nvs_open_from_partition(const char *partition,const char *ns,int mode,int *handle) {
 int store=partition_index(partition),i=namespace_index(ns);
 int err=operation();if(err!=ESP_OK)return err;
 if(mode==NVS_READONLY&&!sizes[store][i])return ESP_ERR_NVS_NOT_FOUND;
 *handle=store*10+i;memcpy(staged[store][i],disk[store][i],64);staged_sizes[store][i]=sizes[store][i];
 return ESP_OK;
}
static int nvs_get_blob(int h,const char *key,void *data,size_t *size) {
 (void)key;int err=operation();if(err!=ESP_OK)return err;
 int store=h/10,i=h%10;
 if(i==5&&marker_error)return marker_error;
 if(!sizes[store][i])return ESP_ERR_NVS_NOT_FOUND;
 if(*size<sizes[store][i]){*size=sizes[store][i];return ESP_ERR_NVS_INVALID_LENGTH;}
 *size=sizes[store][i];memcpy(data,disk[store][i],*size);return ESP_OK;
}
static int nvs_set_blob(int h,const char *key,const void *data,size_t size) {
 (void)key;int err=operation();if(err!=ESP_OK)return err;
 int store=h/10,i=h%10;assert(store==1&&size<=64);
 memcpy(staged[store][i],data,size);staged_sizes[store][i]=size;return ESP_OK;
}
static int nvs_commit(int h) {
 int err=operation();if(err!=ESP_OK)return err;
 int store=h/10,i=h%10;
 memcpy(disk[store][i],staged[store][i],64);sizes[store][i]=staged_sizes[store][i];return ESP_OK;
}
static int nvs_erase_all(int h) {
 int store=h/10,i=h%10;assert(store==1&&i==0);
 memset(staged[store][i],0,64);staged_sizes[store][i]=0;return ESP_OK;
}
static void nvs_close(int h) {(void)h;}
static void seed(void) {
 memset(disk,0,sizeof(disk));memset(sizes,0,sizeof(sizes));
 for(int store=0;store<3;store++)for(int i=0;i<7;i++) {
  disk[store][i][0]=(uint8_t)(20+i);sizes[store][i]=1;
 }
 memset(disk[1][5],1,32);sizes[1][5]=32;memset(recovery,77,sizeof(recovery));
 image.app_elf_sha256[0]=2;step=fail_step=0;erases[0]=erases[1]=0;
 init_error=marker_error=0;init_error_store=1;partial_erase=false;
}
static void assert_protected(void) {
 for(int i=0;i<7;i++)assert(sizes[2][i]==1&&disk[2][i][0]==20+i);
 for(unsigned i=0;i<sizeof(recovery);i++)assert(recovery[i]==77);
}
'''
        for sig in ['static esp_err_t init_partition(', 'static esp_err_t clear_data_namespace(', 'esp_err_t pdkpass_cache_init(',
                    'esp_err_t pdkpass_cache_read_blob(', 'esp_err_t pdkpass_cache_write_blob(',
                    'void pdkpass_cache_forget_namespace(']:
            code += function(source, sig)
        code += r'''
int main(void) {
 uint8_t before[3][7][64],out[64];size_t size;
 seed();memcpy(before,disk,sizeof(before));
 assert(pdkpass_cache_init()==ESP_OK&&s_ready);assert(!memcmp(before,disk,sizeof(before)));
 assert(!erases[0]&&!erases[1]); // A different legacy owner does not erase any data.
 image.app_elf_sha256[0]=1;
 assert(pdkpass_cache_init()==ESP_OK);assert(!memcmp(before,disk,sizeof(before)));
 sizes[1][5]=0;assert(pdkpass_cache_init()==ESP_OK&&!erases[0]&&!erases[1]);
 sizes[1][5]=64;marker_error=ESP_FAIL;
 assert(pdkpass_cache_init()==ESP_OK&&!erases[0]&&!erases[1]); // Marker never read.
 const uint8_t fresh[]={7,8};
 assert(pdkpass_cache_write_blob("pdk_season","current",fresh,2)==ESP_OK);
 assert(pdkpass_cache_init()==ESP_OK);
 size=sizeof(out);assert(pdkpass_cache_read_blob("pdk_season","current",out,&size)==ESP_OK);
 assert(size==2&&!memcmp(out,fresh,2));assert_protected();
 pdkpass_cache_forget_namespace("pdk_season");
 size=sizeof(out);assert(pdkpass_cache_read_blob("pdk_season","current",out,&size)==ESP_ERR_NVS_NOT_FOUND);
 assert(sizes[1][1]==1&&sizes[1][3]==1&&sizes[0][4]==1); // Results, reminders, Wi-Fi intact.
 for(int error=ESP_ERR_NVS_NO_FREE_PAGES;error<=ESP_ERR_NVS_NEW_VERSION_FOUND;error++) {
  seed();memcpy(before,disk,sizeof(before));init_error=error;
  assert(pdkpass_cache_init()==ESP_OK&&s_ready&&erases[0]==0&&erases[1]==1);
  assert(!memcmp(before[0],disk[0],sizeof(disk[0])));assert_protected();
 }
 for(int error=ESP_ERR_NVS_NO_FREE_PAGES;error<=ESP_ERR_NVS_NEW_VERSION_FOUND;error++) {
  seed();memcpy(before,disk,sizeof(before));init_error=error;init_error_store=0;
  assert(pdkpass_cache_init()==ESP_OK&&s_ready&&erases[0]==1&&erases[1]==0);
  assert(!memcmp(before[1],disk[1],sizeof(disk[1])));assert_protected();
 }
 for(unsigned failure=1;failure<=2;failure++) {
  seed();memcpy(before,disk,sizeof(before));fail_step=failure;
  assert(pdkpass_cache_init()!=ESP_OK&&!s_ready&&!erases[0]&&!erases[1]);
  assert(!memcmp(before,disk,sizeof(before)));size=sizeof(out);
  assert(pdkpass_cache_read_blob("pdk_season","current",out,&size)==ESP_ERR_INVALID_STATE);
  assert(pdkpass_cache_write_blob("pdk_season","current",fresh,2)==ESP_ERR_INVALID_STATE);
  fail_step=0;assert(pdkpass_cache_init()==ESP_OK);assert(!memcmp(before,disk,sizeof(before)));
 }
 seed();init_error=ESP_ERR_NVS_NO_FREE_PAGES;fail_step=3;
 assert(pdkpass_cache_init()==ESP_FAIL&&!s_ready&&!erases[0]&&!erases[1]);
 fail_step=0;assert(pdkpass_cache_init()==ESP_OK);assert_protected();
 seed();init_error=ESP_ERR_NVS_NEW_VERSION_FOUND;partial_erase=true;
 assert(pdkpass_cache_init()==ESP_FAIL&&!s_ready&&!erases[0]&&erases[1]==1);
 assert(pdkpass_cache_init()==ESP_OK&&s_ready);assert(sizes[0][4]==1);assert_protected();
 puts("Upgrade/restart data retention, legacy markers, isolated NVS recovery and protected factory regions: PASS");
}
'''
        compile_run(code)

    def test_cache_partition_and_snapshot_replacement_budget(self):
        source=production_source(ROOT / 'main/pdkpass_results.c')
        types=source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code=PRELUDE+'#include "pdkpass_reminder_core.h"\n'+types+r'''
int main(void) {
 size_t season=sizeof(pdkpass_season_snapshot_t)+8;
 size_t teams=sizeof(pdkpass_team_snapshot_t)+8;
 size_t results=sizeof(results_store_t);
 size_t reminders=sizeof(pdkpass_reminder_schedule_t)+8;
 size_t largest=season;
 if(teams>largest)largest=teams;if(results>largest)largest=results;if(reminders>largest)largest=reminders;
 size_t footprint=season+teams+results+reminders;
 assert(footprint+largest+8192<0x10000);
 printf("Cache bytes=%zu; largest replacement=%zu; 64 KB partition has compaction headroom: PASS\n",footprint,largest);
}
'''
        compile_run(code)
        entries=[]
        for line in (ROOT/'partitions.csv').read_text().splitlines():
            if not line.strip() or line.lstrip().startswith('#'):continue
            fields=[x.strip() for x in line.split(',')]
            size=fields[4]
            multiplier=1024 if size.endswith('K') else 1024*1024 if size.endswith('M') else 1
            entries.append((fields[0],int(fields[3],0),int(size.rstrip('KM'),0)*multiplier))
        by_name={name:(offset,size) for name,offset,size in entries}
        self.assertEqual(by_name['pdk_cache'],(0x310000,0x10000))
        self.assertEqual(by_name['nvs'],(0x9000,0x6000))
        self.assertEqual(by_name['factory'],(0x10000,0x300000))
        self.assertEqual(by_name['cardid'],(0x356000,0x4000))
        self.assertEqual(by_name['recovery'],(0x700000,0x100000))
        ordered=sorted(entries,key=lambda x:x[1])
        for left,right in zip(ordered,ordered[1:]):
            self.assertLessEqual(left[1]+left[2],right[1])


if __name__ == '__main__':
    unittest.main()
