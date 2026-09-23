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
static void capture_log(const char *format,...) {
 va_list args;va_start(args,format);
 vsnprintf(last_log,sizeof(last_log),format,args);
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
static int64_t s_retry_at_us;
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
 log_calls=0;last_log[0]='\0';s_retry_at_us=0;
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
 now_us=3000000;status_code=200;retry_after=NULL;chunks[0]="ok";chunk_count=1;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_OK);free(json);
 reset_request();now_us=1000000;status_code=503;
 assert(pdkpass_http_get("https://example.test",16,&json)==ESP_ERR_INVALID_RESPONSE);
 assert(s_retry_at_us==61000000);
 reset_request();chunks[0]="[{\"x\":1},";chunks[1]="{\"x\":2}]";chunk_count=2;
 int items=0;
 assert(pdkpass_http_array("https://example.test",accept_item,&items)==ESP_OK);
 assert(items==2&&log_calls==0);
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
 assert(c->max_freq_mhz==160 && c->min_freq_mhz==80 && c->light_sleep_enable);
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
                       source.index('} season_cache_t;') + len('} season_cache_t;')]
        code = PRELUDE + defines + '\n' + types + r'''
#define NVS_READONLY 0
typedef int nvs_handle_t;
static const char *NVS_NAMESPACE="test", *NVS_KEY="test";
static pdkpass_season_snapshot_t s_season;
static bool s_has_cached_data;
static season_cache_t payload;
static int nvs_open(const char *n,int m,int *h) {(void)n;(void)m;*h=1;return 0;}
static int nvs_get_blob(int h,const char *k,void *out,size_t *size) {
 (void)h;(void)k;assert(*size==sizeof(payload));memcpy(out,&payload,*size);return 0;
}
static void nvs_close(int h) {(void)h;}
'''
        for signature in ['static void copy_text(', 'static void apply_track_details(', 'static void initialize_fallback(',
                          'static bool snapshot_valid(', 'static void load_cache(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 initialize_fallback();assert(s_season.race_count==23);
 payload.magic=SEASON_CACHE_MAGIC;payload.version=SEASON_CACHE_VERSION;
 payload.season=s_season;payload.season.race_count=11;
 memcpy(payload.season.races,pdkpass_races+12,11*sizeof(pdkpass_race_t));
 payload.season.drivers[0].points_tenths=999;
 strcpy(payload.season.standings_as_of,"15 SEP");
 load_cache();
 assert(s_season.race_count==23);assert(snapshot_valid(&s_season));
 assert(s_has_cached_data);
 assert(s_season.drivers[0].points_tenths==999);
 assert(strcmp(s_season.standings_as_of,"15 SEP")==0);
 payload.season.year=2027;load_cache();assert(s_season.race_count==11);
 payload.magic=0;load_cache();assert(s_season.race_count==23);
 assert(!s_has_cached_data);
 puts("season fallback and legacy cache load: PASS");
}
'''
        compile_run(code, ['main/pdkpass_data.c', 'main/pdkpass_tracks.c', 'main/pdkpass_calendar.c'])

    def test_results_scheduling(self):
        source = (ROOT / 'main/pdkpass_results.c').read_text()
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code = PRELUDE + defines + '\n' + types + r'''
#include "pdkpass_sync_policy.h"
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static size_t s_requested_race = SIZE_MAX, s_history_cursor;
static bool s_cache_dirty;
static pdkpass_results_callback_t s_callback;
static bool discover_ok, fetch_ok;
static int discover_calls, fetch_calls, marks;
static bool discover_sessions(size_t i, race_cache_t *cache, int64_t now) {
 (void)i; (void)cache; (void)now; discover_calls++; return discover_ok;
}
static bool fetch_result(session_cache_t *session) {
 fetch_calls++; if (fetch_ok) session->ready=1; return fetch_ok;
}
static esp_err_t save_cache(void) {return ESP_OK;}
void pdkpass_sync_mark_success(pdkpass_sync_service_t service,int64_t utc) {
 (void)utc;assert(service==PDKPASS_SYNC_RESULTS);marks++;
}
'''
        code = code.replace('static pdkpass_results_callback_t s_callback;', 'static void (*s_callback)(size_t);')
        for signature in ['static int64_t retry_interval_seconds(', 'static bool cache_has_due_result(',
                          'static bool discovery_due(', 'static bool cache_complete(',
                          'static bool race_is_eligible(', 'static bool race_needs_work(',
                          'static size_t select_race(', 'static process_outcome_t process_race(',
                          'static TickType_t next_scheduled_wait(']:
            code += function(source, signature)
        code += r'''
int main(void) {
 int64_t now = 1788688800LL;
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
 assert(marks==0); // Discovery and failed result fetch do not claim fresh results.
 assert(!race_needs_work(1,now+11));
 assert(race_needs_work(1,now+611));
 fetch_ok=true; process_race(1,now+611);
 assert(marks==1);
 assert(s_cache[0].next_discovery_utc==now+86400);
 puts("service results scheduling: PASS");
}
'''
        compile_run(code, ['main/pdkpass_results_core.c'])

    def test_partial_season_does_not_replace_cache(self):
        source = (ROOT / 'main/pdkpass_season.c').read_text()
        code = PRELUDE + r'''
#define ESP_ERR_NO_MEM 0x101
typedef struct {pdkpass_race_t race;int64_t start_utc,meeting_end_utc;} race_build_t;
typedef struct {race_build_t *build;size_t count;bool sprint_qualifying[7];
 int64_t now_utc;int latest_race_session;int64_t latest_race_end;} build_context_t;
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
static bool fetch_standings(int key,int64_t date,pdkpass_season_snapshot_t *out) {(void)key;(void)date;(void)out;return false;}
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
 memset(&candidate,0xa5,sizeof(candidate));
 bool retry=false;sessions_fail=true;
 assert(!build_candidate(2026,1788688800,&current,&candidate,&retry));
 assert((unsigned char)candidate.standings_as_of[0]==0xa5);
 sessions_fail=false;
 assert(build_candidate(2026,1788688800,&current,&candidate,&retry));
 assert(strcmp(candidate.races[0].race_cn,current.races[0].race_cn)==0);
 puts("partial season merge: PASS");
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
static size_t s_cache_count, s_requested_race;
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
static uint32_t contrast_color(uint32_t x) {(void)x;return 0xFFFFFF;}
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
static void httpd_resp_set_status(httpd_req_t *r,const char *status) {(void)r;response_status=atoi(status);}
static void httpd_resp_set_type(httpd_req_t *r,const char *type) {(void)r;(void)type;}
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
