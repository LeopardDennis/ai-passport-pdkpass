"""Execute production service functions with deterministic transport/RTOS fakes.

No ESP-IDF download is needed for this host gate. Function bodies and cache
layouts come directly from the production C files, not a copy of their logic.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

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

    def test_reminder_persistence_failure_and_calendar(self):
        source = (ROOT / 'main/pdkpass_reminder.c').read_text()
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
        source = (ROOT / 'main/pdkpass_sound.c').read_text()
        main_source = (ROOT / 'main/main.c').read_text()
        pcm = (ROOT / 'assets/music/session_reminder_pcm.inc').read_text()
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
        source = (ROOT / 'main/pdkpass_sound.c').read_text()
        main_source = (ROOT / 'main/main.c').read_text()
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
static bool init_fail, write_fail, coalesced, alerts_enabled=true;
static bool queued_disabled_result;
bool pdkpass_reminder_enabled(void) {return alerts_enabled;}
static atomic_bool s_reminder_active, s_reminder_cancelled;
static void play_reminder(void) {assert(false);}
static bool pdkpass_sound_consume_key(bsp_btn_t btn,bsp_btn_ev_t ev) {(void)btn;(void)ev;return false;}
static pdkpass_sound_kind_t last_kind, rendered[4];
static int bsp_audio_init(void) {init_calls++;return init_fail ? -1 : ESP_OK;}
static int bsp_audio_set_format(unsigned hz,unsigned bits,unsigned channels) {
 assert(hz==16000 && bits==16 && channels==1);opens++;return ESP_OK;
}
static void bsp_audio_set_volume(unsigned value) {volume=value;}
static void bsp_audio_stop(void) {stops++;}
static int bsp_audio_write(const void *pcm,size_t bytes) {
 assert(pcm==s_pcm && bytes==PDKPASS_SOUND_SAMPLES*sizeof(int16_t));
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
                          if line.startswith('#define SOUND_')) + '\n'
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
 puts("Press dispatch, warm audio reuse, idle stop and audio failure cleanup: PASS");
}
"""
        compile_run(code)

    def test_http_failure_stages_memory_and_backoff(self):
        source = (ROOT / 'main/pdkpass_http.c').read_text()
        types = source[source.index('typedef struct {'):source.index('} heap_sample_t;') + len('} heap_sample_t;')]
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
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_TIMEOUT 4
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
} esp_http_client_config_t;
static void *esp_crt_bundle_attach;
struct fake_client {esp_http_client_config_t config;int status;};
static struct fake_client client;
static int init_calls, cleanup_calls, log_calls, status_code=200;
static int64_t now_us;
static bool init_fails, realloc_fails;
static esp_err_t transport_error;
static const char *retry_after, *chunks[2];
static int chunk_count;
static size_t free_bytes=100000, largest_block=65000;
static char last_log[256];
static char endpoint_log[64];
static void capture_log(const char *format,...) {
 va_list args;va_start(args,format);
 vsnprintf(last_log,sizeof(last_log),format,args);
 if(strstr(last_log,"GET endpoint="))
  snprintf(endpoint_log,sizeof(endpoint_log),"%s",last_log);
 va_end(args);log_calls++;
}
#define ESP_LOGW(tag,format,...) capture_log(format,__VA_ARGS__)
static size_t heap_caps_get_free_size(int caps) {(void)caps;return free_bytes;}
static size_t heap_caps_get_largest_free_block(int caps) {(void)caps;return largest_block;}
static size_t heap_caps_get_minimum_free_size(int caps) {(void)caps;return 70000;}
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
static esp_err_t esp_http_client_perform(esp_http_client_handle_t c) {
 free_bytes=82000;largest_block=42000;
 if (transport_error) return transport_error;
 if (retry_after) {
  esp_http_client_event_t header={.event_id=HTTP_EVENT_ON_HEADER,
   .header_key="Retry-After",.header_value=retry_after,
   .user_data=c->config.user_data,.client=c};
  esp_err_t err=c->config.event_handler(&header);if(err) return err;
 }
 for (int i=0;i<chunk_count;i++) {
  esp_http_client_event_t data={.event_id=HTTP_EVENT_ON_DATA,
   .data=chunks[i],.data_len=(int)strlen(chunks[i]),
   .user_data=c->config.user_data,.client=c};
  esp_err_t err=c->config.event_handler(&data);if(err) return err;
 }
 return ESP_OK;
}
static void esp_http_client_cleanup(esp_http_client_handle_t c) {
 (void)c;cleanup_calls++;free_bytes=98000;largest_block=64000;
}
static void *injected_realloc(void *p,size_t size) {
 if (realloc_fails) return NULL;
 return realloc(p,size);
}
#define realloc injected_realloc
static int64_t s_retry_at_us, s_jolpica_retry_at_us;
static const char *TAG="pdk_http_test";
'''
        code += types + '\n'
        for signature in ['static heap_sample_t sample_heap(',
                          'static void report_failure(',
                          'void pdkpass_http_report_data_failure(',
                          'static bool stream_item(', 'static esp_err_t http_event(',
                          'static esp_err_t perform(', 'esp_err_t pdkpass_http_get(',
                          'esp_err_t pdkpass_http_array(']:
            code += function(source, signature)
        code += r'''
static void reset_request(void) {
 status_code=200;transport_error=ESP_OK;retry_after=NULL;
 chunk_count=0;init_fails=false;realloc_fails=false;
 free_bytes=100000;largest_block=65000;
 log_calls=0;last_log[0]='\0';endpoint_log[0]='\0';s_retry_at_us=0;s_jolpica_retry_at_us=0;
}
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
 assert(!json&&strstr(last_log,"stage=body-limit")&&strstr(last_log,"low=70000"));
 assert(strstr(last_log,"100000/65000 -> 82000/42000 -> 98000/64000"));
 reset_request();chunks[0]="ok";chunk_count=1;realloc_fails=true;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_NO_MEM);
 assert(!json&&strstr(last_log,"stage=body-alloc"));
 reset_request();transport_error=ESP_FAIL;status_code=0;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_FAIL);
 assert(strstr(last_log,"stage=transport"));
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
 puts("HTTP failure stages, heap samples, bounded responses and backoff: PASS");
}
'''
        compile_run(code, ['main/pdkpass_json_stream.c'])

    def test_streamed_results_publish_only_complete_responses(self):
        source = (ROOT / 'main/pdkpass_results.c').read_text()
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
  a.next=&b;b.next=&c;c.next=&d;d.next=&e;cJSON obj={.child=&a};
  assert(item(&obj,ctx));return fail_stream?ESP_FAIL:ESP_OK;
 }
 if(strstr(url,"/session_result?")) {
  for(int i=1;i<=3;i++) {
   cJSON a={.key="position",.valueint=i,.type=2};
   cJSON b={.key="driver_number",.valueint=i,.type=2};
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
 cJSON a={.key="driver_number",.valueint=i,.type=2};
 cJSON b={.key="name_acronym",.valuestring=codes[i-1],.type=1};
 a.next=&b;cJSON obj={.child=&a};assert(item(&obj,ctx));
 return fail_stream || i==fail_driver ? ESP_FAIL : ESP_OK;
}
'''
        for signature in ['static bool json_bool(', 'static void update_session_identity(',
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
 puts("streamed results commit only complete responses: PASS");
}
'''
        compile_run(code)

    def test_jolpica_standings_atomic_pages_and_validation(self):
        source = (ROOT / 'main/pdkpass_season.c').read_text()
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
                          'static bool jolpica_standings_date(', 'static bool fetch_standings(']:
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
        source = (ROOT / 'main/pdkpass_season.c').read_text()
        code = PRELUDE + r'''
#define ESP_ERR_NO_MEM 0x101
static pdkpass_season_snapshot_t s_season;
static bool s_has_cached_data,calendar_ok,points_ok,save_ok=true;
static int saves,callbacks,fetches;
static void callback(void) {callbacks++;}
static pdkpass_season_callback_t s_callback=callback;
bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *out) {*out=s_season;return true;}
static void pdkpass_http_report_data_failure(const char *stage,esp_err_t err,size_t bytes) {(void)stage;(void)err;(void)bytes;}
static bool build_candidate(unsigned year,const pdkpass_season_snapshot_t *current,pdkpass_season_snapshot_t *out) {
 *out=*current;if(!calendar_ok){memset(out,0xa5,sizeof(*out));return false;}
 out->year=year;out->races[0].laps=60;return true;
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
 points_ok=true;save_ok=false;assert(!synchronize(now));assert(!memcmp(&old,&s_season,sizeof(old))&&!callbacks);
 save_ok=true;assert(!synchronize(now)); // Calendar still needs a retry, but points publish.
 assert(s_season.drivers[0].points_tenths==2920&&s_season.races[0].meeting_key==99&&callbacks==1);
 assert(s_has_cached_data&&fetches==2);
 old=s_season;points_ok=false;assert(!synchronize(now));assert(!memcmp(&old,&s_season,sizeof(old)));
 calendar_ok=true;assert(!synchronize(now));assert(s_season.races[0].laps==60&&s_season.drivers[0].points_tenths==2920);
 points_ok=true;assert(synchronize(now));
 int previous=fetches;calendar_ok=false;assert(!synchronize(now+366LL*86400));assert(fetches==previous);
 puts("Independent Jolpica sync, retry and persisted atomic publication: PASS");
}
'''
        compile_run(code, ['main/pdkpass_results_core.c', 'main/pdkpass_season_core.c'])

    def test_jolpica_constructor_pages(self):
        source = (ROOT / 'main/pdkpass_season.c').read_text()
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
        source = (ROOT / 'main/pdkpass_season.c').read_text()
        types = source[source.index('#define TEAM_CACHE_MAGIC'):source.index('static bool s_has_cached_data;')]
        code = PRELUDE + types + r'''
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
 disk.version=99;load_team_cache();assert(!s_teams.count);
 disk.version=1;disk.snapshot.count=PDKPASS_MAX_TEAMS+1;load_team_cache();assert(!s_teams.count);
 disk.snapshot.count=1;memset(disk.snapshot.teams[0].id,'X',sizeof(disk.snapshot.teams[0].id));load_team_cache();assert(!s_teams.count);
 puts("Team NVS: failed commits, restart, unchanged data and cross-season isolation: PASS");
}
'''
        compile_run(code)

    def test_dark_display_skips_battery_i2c(self):
        source = (ROOT / 'main/main.c').read_text()
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
        source = (ROOT / 'main/pdkpass_network.c').read_text()
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
        source = (ROOT / 'main/pdkpass_season.c').read_text()
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

    def test_circuit_metadata_is_independent_of_season(self):
        source = (ROOT / 'main/pdkpass_season.c').read_text()
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
        source = (ROOT / 'main/pdkpass_results.c').read_text()
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
        source = (ROOT / 'main/pdkpass_season.c').read_text()
        code = PRELUDE + r'''
#include <setjmp.h>
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
static void finish_points_force(pdkpass_manual_state_t state) {(void)state;}
static pdkpass_manual_state_t synchronize_manual_points(int64_t now) {
 (void)now;assert(false);return PDKPASS_MANUAL_FAILED;
}
static void refresh_builtin_year(int64_t now) {(void)now;}
static TickType_t offline_wait(uint32_t wait,int64_t now) {(void)now;return wait?wait:portMAX_DELAY;}
void pdkpass_network_request(pdkpass_network_command_t c) {assert(c==PDKPASS_NETWORK_SYNC);requests++;}
uint32_t pdkpass_sync_wait_ms(pdkpass_sync_service_t s) {return pdkpass_sync_policy_wait(&policy,s,now_ms);}
void pdkpass_sync_plan(pdkpass_sync_service_t s,uint32_t delay) {policy.due_ms[s]=now_ms+delay;}
static void pdkpass_http_begin(void) {transactions++;if(disconnect_on_lock) online=false;}
static void pdkpass_http_end(void) {}
static bool synchronize(int64_t now) {(void)now;synchronizations++;return true;}
void pdkpass_sync_mark_success(pdkpass_sync_service_t service,int64_t utc) {
 (void)utc;assert(service==PDKPASS_SYNC_SEASON);
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
        source = (ROOT / 'components/bsp/src/bsp_battery.c').read_text()
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
                          'int bsp_battery_soc(', 'int bsp_battery_mv(',
                          'esp_err_t bsp_battery_read_diagnostics(']:
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
 bsp_battery_diagnostics_t sample;
 unsigned previous_writes=writes;
 fail_read=-1;raw_soc=96*256+128;config=0;
 assert(bsp_battery_read_diagnostics(&sample)==ESP_OK);
 assert(sample.raw_soc==24704&&sample.raw_vcell==11840&&sample.cell_mv==3700);
 assert(sample.config==0&&sample.version==0xa0&&writes==previous_writes);
 // Each failed register is distinguishable; other successful readings survive.
 const int registers[]={0,2,4,8};
 for(unsigned i=0;i<4;i++) {
  fail_read=registers[i];
  assert(bsp_battery_read_diagnostics(&sample)==ESP_FAIL);
  assert(sample.raw_soc==(fail_read==4?-1:24704));
  assert(sample.cell_mv==(fail_read==2?-1:3700));
  assert(sample.config==(fail_read==8?-1:0));
  assert(sample.version==(fail_read==0?-1:0xa0));
 }
 fail_read=-1;raw_soc=255*256;config=0xf0;
 assert(bsp_battery_read_diagnostics(&sample)==ESP_OK);
 assert(sample.raw_soc==65280&&sample.config==0xf0); // report, never repair/reset
 assert(writes==previous_writes);
 s_dev=NULL;
 assert(bsp_battery_read_diagnostics(&sample)==ESP_ERR_INVALID_STATE);
 assert(sample.raw_soc==-1&&sample.cell_mv==-1&&sample.config==-1&&sample.version==-1);
 assert(bsp_battery_read_diagnostics(NULL)==ESP_ERR_INVALID_ARG);
}
'''
        compile_run(code)

    def test_battery_diagnostics_interval_and_disabled_build(self):
        source = (ROOT / 'main/pdkpass_battery_diagnostics.c').read_text()
        for header in ('sdkconfig.h', 'esp_log.h', 'esp_timer.h'):
            source = source.replace('#include "' + header + '"', '')
        for enabled in (0, 1):
            code = '#define CONFIG_PDKPASS_BATTERY_DIAGNOSTICS %d\n' % enabled
            code += r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "bsp_battery.h"
static int64_t now;
static unsigned reads, logs;
static int fail;
static int raw_soc=24704, cell_mv=3700;
static char last_log[512];
static int64_t esp_timer_get_time(void) { return now; }
esp_err_t bsp_battery_read_diagnostics(bsp_battery_diagnostics_t *out) {
 reads++;now+=2000;
 *out=(bsp_battery_diagnostics_t){.raw_soc=raw_soc,.raw_vcell=11840,
  .cell_mv=cell_mv,.config=0,.version=160};
 if(fail) {out->raw_soc=-1;out->cell_mv=-1;out->config=-1;return -1;}
 return 0;
}
static void capture(const char *tag,const char *fmt,...) {
 assert(strcmp(tag,"battery_diag")==0);logs++;
 va_list args;va_start(args,fmt);vsnprintf(last_log,sizeof(last_log),fmt,args);va_end(args);
}
#define ESP_LOGI(...) capture(__VA_ARGS__)
""" + source + r"""
int main(void) {
#if CONFIG_PDKPASS_BATTERY_DIAGNOSTICS
 assert(pdkpass_battery_diagnostics_wait_ms()==0);
 pdkpass_battery_diagnostics_poll();
 assert(reads==1&&logs==1);
 assert(strstr(last_log,"soc_x100=9650 soc_valid=1"));
 assert(strstr(last_log,"display_soc=96 soc_fraction_256=128"));
 assert(strstr(last_log,"cell_delta_mv=0 cell_delta_valid=0"));
 assert(strstr(last_log,"cell_mv=3700"));
 assert(strstr(last_log,"config=0 mode=ACTIVE version=160 read_error=0"));
 assert(pdkpass_battery_diagnostics_wait_ms()==60000);
 // Repeated key/reminder wakeups do not cause extra reads or output.
 for(unsigned i=0;i<100;i++)pdkpass_battery_diagnostics_poll();
 assert(reads==1&&logs==1);
 now+=59999999;
 assert(pdkpass_battery_diagnostics_wait_ms()==1);
 pdkpass_battery_diagnostics_poll();assert(reads==1);
 now++;raw_soc=99*256+255;cell_mv=4180;
 pdkpass_battery_diagnostics_poll();assert(reads==2&&logs==2);
 assert(strstr(last_log,"display_soc=99 soc_fraction_256=255"));
 assert(strstr(last_log,"cell_delta_mv=480 cell_delta_valid=1"));
 fail=1;now+=60000000;
 pdkpass_battery_diagnostics_poll();assert(reads==3&&logs==3);
 assert(strstr(last_log,"soc_raw=-1 soc_x100=-1 soc_valid=0"));
 assert(strstr(last_log,"display_soc=-1 soc_fraction_256=-1"));
 assert(strstr(last_log,"cell_delta_valid=0"));
 assert(strstr(last_log,"mode=UNKNOWN")&&strstr(last_log,"read_error=-1"));
 assert(pdkpass_battery_diagnostics_wait_ms()==60000);
 fail=0;raw_soc=100*256;cell_mv=4200;now+=60000000;
 pdkpass_battery_diagnostics_poll();assert(reads==4&&logs==4);
 assert(strstr(last_log,"display_soc=100 soc_fraction_256=0"));
 assert(strstr(last_log,"cell_delta_valid=0"));
#else
 for(unsigned i=0;i<100;i++) {
  now+=60000000;pdkpass_battery_diagnostics_poll();
  assert(pdkpass_battery_diagnostics_wait_ms()==UINT32_MAX);
 }
 assert(reads==0&&logs==0);
#endif
 puts("Battery diagnostics: rate limit, partial failures and disabled mode: PASS");
}
"""
            compile_run(code)

    def test_panel_sleep_transitions_and_failure_retry(self):
        source = (ROOT / 'components/bsp/src/bsp_display.c').read_text()
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
        source = (ROOT / 'main/pdkpass_power.c').read_text()
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
        source = (ROOT / 'main/pdkpass_network.c').read_text()
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
        source = (ROOT / 'main/pdkpass_network.c').read_text()
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
        source = (ROOT / 'main/pdkpass_network.c').read_text()
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
        source = (ROOT / 'main/pdkpass_season.c').read_text()
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
        source = (ROOT / 'main/pdkpass_results.c').read_text()
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        code = PRELUDE + defines + r"""
#include <setjmp.h>
#include "pdkpass_sync_policy.h"
#define EVENT_WAKE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define PDKPASS_NETWORK_SYNC 1
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
static bool take_manual_race(size_t *race) {(void)race;return false;}
static void finish_manual_race(size_t race,pdkpass_manual_state_t state) {
 (void)race;(void)state;assert(false);
}
static void process_manual_race(size_t race,int64_t utc) {
 (void)race;(void)utc;assert(false);
}
static void pdkpass_network_request(int command) {(void)command;assert(false);}
static void pdkpass_http_begin(void) {begins++;}
static void pdkpass_http_end(void) {ends++;}
static int save_cache(void) {return ESP_OK;}
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

    def test_manual_results_refreshes_only_selected_round_and_preserves_cache(self):
        source = (ROOT / 'main/pdkpass_results.c').read_text()
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code = PRELUDE + types + r'''
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static size_t s_force_race=1;
static pdkpass_manual_status_t s_force_status={.state=PDKPASS_MANUAL_RUNNING};
static bool s_cache_dirty, fail_one;
static int discoveries, fetches, saves, callbacks, cues;
static void vTaskDelay(unsigned ticks) {(void)ticks;}
static void callback(size_t race,bool first) {
 assert(race==1);callbacks++;if(first)cues++;
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
 assert(discoveries==1&&fetches==2&&saves==1&&callbacks==1&&cues==0);
 assert(s_force_status.state==PDKPASS_MANUAL_UPDATED);
 assert(strcmp(s_cache[1].sessions[0].podium_codes[0],"NEW")==0);
 assert(strcmp(s_cache[1].sessions[1].podium_codes[0],"FIX")==0);
 assert(s_cache[0].meeting_key==456&&s_cache[0].sessions[0].ready==0);
 s_force_status.state=PDKPASS_MANUAL_RUNNING;
 process_manual_race(1,101);
 assert(fetches==4&&saves==1&&s_force_status.state==PDKPASS_MANUAL_UNCHANGED);
 s_force_status.state=PDKPASS_MANUAL_RUNNING;fail_one=true;
 process_manual_race(1,102);
 assert(s_force_status.state==PDKPASS_MANUAL_FAILED&&saves==1);
 assert(strcmp(s_cache[1].sessions[1].podium_codes[0],"FIX")==0);
 fail_one=false;s_force_status.state=PDKPASS_MANUAL_RUNNING;
 for(int i=0;i<2;i++) {
  s_cache[1].sessions[i].ready=0;
  memset(s_cache[1].sessions[i].podium_codes,0,
         sizeof(s_cache[1].sessions[i].podium_codes));
 }
 process_manual_race(1,103);
 assert(saves==2&&cues==2&&s_force_status.state==PDKPASS_MANUAL_UPDATED);
 puts("Manual results: selected round, corrections, unchanged and failure cache: PASS");
}
'''
        compile_run(code)

    def test_manual_points_refreshes_both_tables_without_calendar(self):
        source = (ROOT / 'main/pdkpass_season.c').read_text()
        code = PRELUDE + r'''
static pdkpass_season_snapshot_t s_season;
static pdkpass_team_snapshot_t s_teams;
static bool s_has_cached_data, team_fails;
static int driver_fetches,team_fetches,driver_saves,team_saves,callbacks;
static void callback(void) {callbacks++;}
static void (*s_callback)(void)=callback;
unsigned pdkpass_beijing_year(int64_t now) {(void)now;return 2026;}
bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *out) {*out=s_season;return true;}
bool pdkpass_season_team_snapshot(pdkpass_team_snapshot_t *out) {*out=s_teams;return true;}
static bool fetch_standings(int64_t now,pdkpass_season_snapshot_t *out) {
 (void)now;driver_fetches++;out->drivers[0].points_tenths+=10;return true;
}
static bool fetch_team_standings(int64_t now,pdkpass_team_snapshot_t *out) {
 (void)now;team_fetches++;
 if(team_fails)return false;
 out->teams[0].points_tenths+=10;return true;
}
static int save_cache(const pdkpass_season_snapshot_t *value) {
 (void)value;driver_saves++;return ESP_OK;
}
static int save_team_cache(const pdkpass_team_snapshot_t *value) {
 (void)value;team_saves++;return ESP_OK;
}
'''
        code += function(source, 'static pdkpass_manual_state_t synchronize_manual_points(')
        code += r'''
int main(void) {
 s_lock=1;s_season.year=2026;s_teams.year=2026;
 s_season.driver_count=1;s_teams.count=1;
 s_season.drivers[0].points_tenths=100;s_teams.teams[0].points_tenths=100;
 assert(synchronize_manual_points(100)==PDKPASS_MANUAL_UPDATED);
 assert(driver_fetches==1&&team_fetches==1&&driver_saves==1&&team_saves==1);
 assert(s_season.drivers[0].points_tenths==110&&s_teams.teams[0].points_tenths==110);
 assert(callbacks==2&&s_has_cached_data);
 team_fails=true;
 assert(synchronize_manual_points(101)==PDKPASS_MANUAL_PARTIAL);
 assert(s_season.drivers[0].points_tenths==120&&s_teams.teams[0].points_tenths==110);
 assert(driver_fetches==2&&team_fetches==2&&callbacks==3);
 puts("Manual points: both tables, partial failure and retained cache: PASS");
}
'''
        compile_run(code)

    def test_results_scheduling(self):
        source = (ROOT / 'main/pdkpass_results.c').read_text()
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
static bool s_cache_dirty;
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
void pdkpass_sync_mark_success(pdkpass_sync_service_t service,int64_t utc) {
 (void)utc;assert(service==PDKPASS_SYNC_RESULTS);marks++;
}
'''
        code = code.replace('static pdkpass_results_callback_t s_callback;',
                            'static void (*s_callback)(size_t,bool);')
        for signature in ['static int64_t retry_interval_seconds(', 'static bool cache_has_due_result(',
                          'static bool discovery_due(', 'static bool cache_complete(',
                          'static bool race_is_eligible(', 'static bool race_needs_work(',
                          'static void expedite_requested_race(',
                          'static size_t select_race(', 'static process_outcome_t process_race(',
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
        source = (ROOT / 'main/pdkpass_season.c').read_text()
        code = PRELUDE + r'''
#define ESP_ERR_NO_MEM 0x101
typedef struct {pdkpass_race_t race;int64_t start_utc,meeting_end_utc;} race_build_t;
typedef struct {race_build_t *build;size_t count;bool sprint_qualifying[PDKPASS_MAX_RACES];} build_context_t;
static void copy_text(char *d,size_t n,const char *s) {snprintf(d,n,"%s",s);}
static bool parse_meeting(const void *item,void *user) {(void)item;(void)user;return true;}
static bool populate_session(const void *item,void *user) {(void)item;(void)user;return true;}
static int compare_races(const void *a,const void *b) {(void)a;(void)b;return 0;}
static bool sessions_fail;
static esp_err_t pdkpass_http_array(const char *url,bool (*item)(const void *,void *),void *user) {
 (void)item;build_context_t *ctx=user;
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
 pdkpass_season_snapshot_t current={.year=2026,.race_count=1}, candidate;
 races[0].meeting_key=10;races[0].round=1;races[0].switch_at_utc=1788688800;
 current.races[0]=races[0];strcpy(current.races[0].race_cn,"RACE 06 SEP 16:00");
 current.driver_count=1;current.drivers[0].points_tenths=2420;
 strcpy(current.standings_as_of,"31 AUG");
 memset(&candidate,0xa5,sizeof(candidate));
 sessions_fail=true;
 assert(!build_candidate(2026,&current,&candidate));
 assert((unsigned char)candidate.standings_as_of[0]==0xa5);
 sessions_fail=false;
 assert(build_candidate(2026,&current,&candidate));
 assert(strcmp(candidate.races[0].race_cn,current.races[0].race_cn)==0);
 // Calendar refresh retains standings; synchronize updates them independently.
 assert(candidate.drivers[0].points_tenths==2420);
 assert(strcmp(candidate.standings_as_of,"31 AUG")==0);
 puts("partial season merge and standings retry: PASS");
}
'''
        compile_run(code, ['main/pdkpass_season_core.c'])

    def test_cache_migration_and_identity(self):
        source = (ROOT / 'main/pdkpass_results.c').read_text()
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
        source = (ROOT / 'main/pdkpass_ui.c').read_text()
        code = '#define _POSIX_C_SOURCE 200809L\n' + PRELUDE + r'''
#include "pdkpass_sync_policy.h"
#define BEIJING_OFFSET_SECONDS 28800LL
'''
        code += function(source, 'static void format_sync_line(')
        code += r'''
int main(void) {
 char line[32];
 format_sync_line(PDKPASS_SYNC_SEASON,2026,0,false,line,sizeof(line));
 assert(strcmp(line,"CAL SYNC NEVER")==0);
 format_sync_line(PDKPASS_SYNC_SEASON,2026,0,true,line,sizeof(line));
 assert(strcmp(line,"CAL CACHE DATE?")==0);
 format_sync_line(PDKPASS_SYNC_RESULTS,2026,0,true,line,sizeof(line));
 assert(strcmp(line,"RESULT CACHE DATE?")==0);
 format_sync_line(PDKPASS_SYNC_RESULTS,2026,0,false,line,sizeof(line));
 assert(strcmp(line,"RESULTS NEVER")==0);
 format_sync_line(PDKPASS_SYNC_SEASON,2026,1767225600LL,true,line,sizeof(line));
 assert(strcmp(line,"CAL SYNC 26.01.01")==0);
 // The UTC boundary at 16:00 is Beijing New Year, not the preceding season.
 format_sync_line(PDKPASS_SYNC_SEASON,2027,1798732799LL,false,line,sizeof(line));
 assert(strcmp(line,"CAL 2027 NO SYNC")==0);
 format_sync_line(PDKPASS_SYNC_RESULTS,2027,1798732799LL,false,line,sizeof(line));
 assert(strcmp(line,"RESULT 2027 NO SYNC")==0);
 format_sync_line(PDKPASS_SYNC_RESULTS,2027,1798732799LL,true,line,sizeof(line));
 assert(strcmp(line,"RESULT CACHE DATE?")==0);
 format_sync_line(PDKPASS_SYNC_SEASON,2027,1798732800LL,false,line,sizeof(line));
 assert(strcmp(line,"CAL SYNC 27.01.01")==0);
 puts("sync menu distinguishes old cache, no cache and dated sync: PASS");
}
'''
        compile_run(code)

    def test_battery_warning_has_contrasting_background(self):
        source = (ROOT / 'main/pdkpass_ui.c').read_text()
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

    def test_wifi_profile_persistence_and_legacy_import(self):
        source = (ROOT / 'main/pdkpass_network.c').read_text()
        code = PRELUDE + r'''
#include "pdkpass_wifi_profiles.h"
#define NVS_NAMESPACE "test"
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_INVALID_ARG 2
typedef int nvs_handle_t;
static pdkpass_wifi_profiles_t s_profiles={.version=1}, persisted, pending;
static char s_working_ssid[33], s_working_password[65];
static bool have_blob, fail_commit;
static int nvs_open(const char *name,int mode,nvs_handle_t *handle) {
 (void)name;(void)mode;*handle=1;return ESP_OK;
}
static void nvs_close(nvs_handle_t handle) {(void)handle;}
static int nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *size) {
 (void)handle;(void)key;if(!have_blob)return ESP_FAIL;
 assert(*size>=sizeof(persisted));memcpy(out,&persisted,sizeof(persisted));
 *size=sizeof(persisted);return ESP_OK;
}
static int nvs_get_str(nvs_handle_t handle,const char *key,char *out,size_t *size) {
 (void)handle;const char *value=strcmp(key,"ssid")==0?"Legacy":"test-only";
 assert(*size>strlen(value));strcpy(out,value);*size=strlen(value)+1;return ESP_OK;
}
static int nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t size) {
 (void)handle;(void)key;assert(size==sizeof(pending));memcpy(&pending,data,size);return ESP_OK;
}
static int nvs_commit(nvs_handle_t handle) {
 (void)handle;if(fail_commit)return ESP_FAIL;persisted=pending;have_blob=true;return ESP_OK;
}
'''
        code += function(source, 'static bool load_credentials(')
        code += function(source, 'static esp_err_t save_credentials(')
        code += r'''
int main(void) {
 assert(load_credentials());assert(s_profiles.count==1);
 assert(strcmp(s_profiles.entries[0].ssid,"Legacy")==0);
 assert(save_credentials(s_working_ssid,s_working_password)==ESP_OK);
 assert(have_blob);assert(save_credentials("Second","test-only")==ESP_OK);
 pdkpass_wifi_profiles_t before=s_profiles;
 fail_commit=true;assert(save_credentials("Third","test-only")==ESP_FAIL);
 assert(memcmp(&before,&s_profiles,sizeof(before))==0);
 memset(&s_profiles,0,sizeof(s_profiles));assert(load_credentials());
 assert(s_profiles.count==2);assert(strcmp(s_working_ssid,"Second")==0);
 persisted.count=6;assert(load_credentials());assert(s_profiles.count==1);
 assert(strcmp(s_working_ssid,"Legacy")==0);
 puts("Wi-Fi legacy migration, reload and failed commit preservation: PASS");
}
'''
        compile_run(code, ['main/pdkpass_wifi_profiles.c'])

    def test_provisioning_uses_one_immutable_attempt(self):
        source = (ROOT / 'main/pdkpass_network.c').read_text()
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
        source = (ROOT / 'main/pdkpass_network.c').read_text()
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
static bool pdkpass_sync_idle(void) {return sync_idle;}
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
static int scan_saved(void) {scan_calls++;s_saved_attempt=0;return connect_saved();}
static int prepare_network(void) {return connect_saved();}
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
        code += function(source, 'static TickType_t network_wait_ticks(')
        code += function(source, 'static void network_task(')
        code += r'''
int main(void) {
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

if __name__ == '__main__':
    unittest.main()
