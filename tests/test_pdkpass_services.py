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
#include "pdkpass_season.h"
#include "pdkpass_results_core.h"
#include "pdkpass_season_core.h"
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
    def test_results_scheduling(self):
        source = (ROOT / 'main/pdkpass_results.c').read_text()
        defines = '\n'.join(x for x in source.splitlines() if x.startswith('#define RESULTS_'))
        types = source[source.index('typedef struct {'):source.index('static const char *TAG')]
        code = PRELUDE + defines + '\n' + types + r'''
static race_cache_t s_cache[PDKPASS_MAX_RACES];
static size_t s_requested_race = SIZE_MAX, s_history_cursor;
static bool s_cache_dirty;
static pdkpass_results_callback_t s_callback;
static bool discover_ok;
static int discover_calls, fetch_calls;
static bool discover_sessions(size_t i, race_cache_t *cache, int64_t now) {
 (void)i; (void)cache; (void)now; discover_calls++; return discover_ok;
}
static bool fetch_result(session_cache_t *session) {(void)session; fetch_calls++; return false;}
static esp_err_t save_cache(void) {return ESP_OK;}
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
 assert(!race_needs_work(1,now+11));
 assert(race_needs_work(1,now+611));
 assert(s_cache[0].next_discovery_utc==now+86400);
 puts("service results scheduling: PASS");
}
'''
        compile_run(code, ['main/pdkpass_results_core.c'])

    def test_partial_season_does_not_replace_cache(self):
        source = (ROOT / 'main/pdkpass_season.c').read_text()
        code = PRELUDE + r'''
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
 puts("cache v2 migration, v3 identity and meeting reorder: PASS");
}
'''
        compile_run(code)

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
#define WIFI_MODE_STA 1
typedef unsigned EventBits_t;
typedef struct {unsigned content_len;const char *body;} httpd_req_t;
typedef struct {unsigned char ssid[33];} wifi_ap_record_t;
static int s_candidate_lock, s_events;
static bool s_candidate_busy, s_testing_candidate, s_has_ip, s_in_setup=true;
static bool s_have_working_credentials;
static int64_t s_candidate_deadline;
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
 (void)event;(void)clear;(void)all;(void)wait;return bits;
}
static int esp_wifi_disconnect(void) {return ESP_ERR_WIFI_NOT_CONNECT;}
static int esp_wifi_connect(void) {return ESP_OK;}
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
 assert(test_candidate()==ESP_OK);assert(s_testing_candidate);
 strcpy(connected_ssid,"WrongAP");
 assert(accept_candidate()==ESP_ERR_INVALID_STATE);assert(!s_have_working_credentials);
 strcpy(connected_ssid,"TestA");
 // Even if the pending slot were corrupted, commit the actual in-flight copy.
 strcpy(s_candidate_ssid,"TestB");strcpy(s_candidate_password,"ijklmnop");
 assert(accept_candidate()==ESP_OK);
 assert(strcmp(saved_ssid,"TestA")==0);assert(strcmp(saved_password,"abcdefgh")==0);
 assert(!s_candidate_busy);assert(!s_testing_candidate);assert(s_attempt_password[0]==0);
 puts("provisioning duplicate rejection and immutable commit: PASS");
}
'''
        compile_run(code, ['main/pdkpass_wifi_form.c'])

    def test_reconnect_does_not_require_a_second_ntp_success(self):
        source = (ROOT / 'main/pdkpass_network.c').read_text()
        code = PRELUDE + r'''
#include <setjmp.h>
#include "pdkpass_network.h"
#define ESP_LOGE(...) ((void)0)
#define EVENT_CONNECTED 1
#define EVENT_DISCONNECTED 2
#define EVENT_CANDIDATE 4
#define EVENT_TIME_SYNCED 8
#define pdFALSE 0
#define WIFI_RETRY_LIMIT 5
static jmp_buf finished;
typedef unsigned EventBits_t;
typedef struct {unsigned char ssid[33];} wifi_ap_record_t;
static int s_events;
static bool s_time_synced_boot, s_has_ip, s_in_setup, s_testing_candidate;
static bool s_have_working_credentials=true;
static int64_t s_candidate_deadline, s_sync_deadline, now_us;
static int online_count, time_error_count, sync_count, cursor, event_count;
static unsigned script[8];
static int64_t times[8];
static unsigned xEventGroupWaitBits(int e,unsigned bits,int clear,int all,unsigned timeout) {
 (void)e;(void)bits;(void)clear;(void)all;(void)timeout;
 if(cursor>=event_count) longjmp(finished,1);
 now_us=times[cursor];return script[cursor++];
}
static int prepare_network(void) {return ESP_OK;}
static void vTaskDelete(void *task) {(void)task;}
static int test_candidate(void) {return ESP_OK;}
static void finish_candidate(void) {s_testing_candidate=false;}
static int start_setup(void) {return ESP_OK;}
static int esp_wifi_connect(void) {return ESP_OK;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {(void)ap;return ESP_OK;}
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
        code += function(source, 'static void network_task(')
        code += r'''
int main(void) {
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
 puts("NTP cold boot and trusted reconnect: PASS");
}
'''
        compile_run(code)

if __name__ == '__main__':
    unittest.main()
