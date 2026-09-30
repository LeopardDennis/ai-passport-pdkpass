// Exercise production widgets and transformed text within the firmware pool.
#define main simulator_application_main
#include "simulator.m"
#undef main
#include <assert.h>
static unsigned dark_timer_fired;
static void dark_timer(lv_timer_t *timer) { (void)timer; dark_timer_fired++; }

static void check_body_text(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const lv_font_t *font = lv_obj_get_style_text_font(obj, 0);
        if (font->line_height == 14) {
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(obj), font,
                             lv_obj_get_style_text_letter_space(obj, 0), 0,
                             LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > lv_obj_get_content_width(obj)) {
                fprintf(stderr, "Clipped body text: %s (%ld > %ld)\n",
                        lv_label_get_text(obj), (long)size.x,
                        (long)lv_obj_get_content_width(obj));
                abort();
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
        check_body_text(lv_obj_get_child(obj, i));
}

static lv_obj_t *find_label(lv_obj_t *obj, const char *text)
{
    if (lv_obj_check_type(obj, &lv_label_class) &&
        strcmp(lv_label_get_text(obj), text) == 0) return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *found = find_label(lv_obj_get_child(obj, i), text);
        if (found) return found;
    }
    return NULL;
}

static void check_memory(void)
{
    simulator_refresh();
    check_body_text(lv_screen_active());
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(lv_mem_test() == LV_RESULT_OK);
    assert(memory.total_size > memory.max_used + 2048);
}

int main(void)
{
    @autoreleasepool {
        pdkpass_season_snapshot_t snapshot;
        assert(pdkpass_season_snapshot(&snapshot));
        assert(snapshot.race_count == pdkpass_race_count);
        for (size_t i = 0; i < snapshot.race_count; i++) {
            snapshot.races[i].switch_at_utc = pdkpass_races[i].switch_at_utc;
            assert(memcmp(&snapshot.races[i], &pdkpass_races[i],
                          sizeof(pdkpass_race_t)) == 0);
        }
        s_home_race_index = 0; // Australia's medium-length name exercises fitted scaling.
        simulator_initialize();
        s_simulator_online = NO; // Keep seeded page fixtures independent of HTTP.
        lv_obj_t *australia = find_label(lv_screen_active(), "AUSTRALIA");
        assert(australia);
        assert(lv_obj_get_style_transform_scale_x(australia, 0) > 256);
        assert(lv_obj_get_style_transform_scale_x(australia, 0) < 512);
        check_memory(); // Includes the home hint's full text width.
        // First use shows no seeded points; a later cache update refreshes in place.
        assert(snapshot.driver_count == 0);
        simulator_send_button(BSP_BTN_UP, BSP_BTN_LONG);
        assert(find_label(lv_screen_active(), "STANDINGS 26"));
        assert(find_label(lv_screen_active(), "DRIVER DATA PENDING"));
        assert(find_label(lv_screen_active(), "CONNECT TO UPDATE"));
        assert(!find_label(lv_screen_active(), "ANTONELLI"));
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
        assert(find_label(lv_screen_active(), "DRIVER DATA PENDING"));
        check_memory();
        s_driver_preview = true;
        pdkpass_ui_season_update();
        assert(find_label(lv_screen_active(), "ANTONELLI"));
        assert(!find_label(lv_screen_active(), "DRIVER DATA PENDING"));
        s_preview_year = 2027;
        pdkpass_ui_season_update();
        assert(find_label(lv_screen_active(), "DRIVER DATA PENDING"));
        assert(!find_label(lv_screen_active(), "ANTONELLI"));
        s_preview_year = 2026;
        pdkpass_ui_season_update();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_LONG);
        assert(find_label(lv_screen_active(), "CONNECT TO UPDATE"));
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
        assert(find_label(lv_screen_active(), "CONNECT TO UPDATE"));
        s_team_preview = true;
        pdkpass_ui_season_update();
        assert(find_label(lv_screen_active(), "TEAM POINTS 26"));
        assert(find_label(lv_screen_active(), "MERCEDES"));
        assert(find_label(lv_screen_active(), "503"));
        for (unsigned i = 0; i < 11; i++) {
            check_memory();
            simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        }
        assert(find_label(lv_screen_active(), "MERCEDES"));
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK);
        assert(find_label(lv_screen_active(), "CADILLAC"));
        s_preview_year = 2027;
        pdkpass_ui_season_update();
        assert(find_label(lv_screen_active(), "CONNECT TO UPDATE"));
        assert(!find_label(lv_screen_active(), "503"));
        check_memory();
        s_preview_year = 2026;
        pdkpass_ui_season_update();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
        assert(find_label(lv_screen_active(), "UP/DN:RACE OK:VIEW"));
        // A maximum-length podium name must occupy one line without clipping.
        [s_results_lock lock];
        for (size_t session = 0; session < PDKPASS_SESSION_COUNT; session++)
            s_results[0][session].status = PDKPASS_RESULT_NOT_HELD;
        pdkpass_result_snapshot_t *sample =
            &s_results[0][PDKPASS_SESSION_FP1];
        sample->status = PDKPASS_RESULT_READY;
        const char *codes[] = {"VER", "HUL", "BOR"};
        const char *names[] = {"VERSTAPPEN", "HULKENBERG", "BORTOLETO"};
        for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
            sample->podium[i].position = (unsigned)i + 1U;
            snprintf(sample->podium[i].code, sizeof(sample->podium[i].code),
                     "%s", codes[i]);
            snprintf(sample->podium[i].name, sizeof(sample->podium[i].name),
                     "%s", names[i]);
        }
        [s_results_lock unlock];
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // home -> detail
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // detail -> results
        assert(find_label(lv_screen_active(), "MAX VERSTAPPEN"));
        assert(find_label(lv_screen_active(), "NICO HULKENBERG"));
        assert(find_label(lv_screen_active(), "GABRIEL BORTOLETO"));
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // stays on results
        assert(find_label(lv_screen_active(), "MAX VERSTAPPEN"));
        assert(find_label(lv_screen_active(), "R1 WIFI OFFLINE"));
        // A downloaded schedule is not a downloaded result. Exercise the
        // historical-session regression and missing timestamps restored from NVS.
        sample->status = PDKPASS_RESULT_SCHEDULED;
        sample->session_end_utc = (int64_t)time(NULL) - 3600;
        pdkpass_ui_results_update(0);
        assert(find_label(lv_screen_active(), "SYNC PENDING"));
        assert(find_label(lv_screen_active(), "RESULT NOT DOWNLOADED"));
        assert(!find_label(lv_screen_active(), "RESULT PENDING"));
        assert(!find_label(lv_screen_active(), "SYNC ABOUT +30 MIN"));
        check_memory();
        sample->session_end_utc = (int64_t)time(NULL) + 3600;
        pdkpass_ui_results_update(0);
        assert(find_label(lv_screen_active(), "RESULT PENDING"));
        sample->session_end_utc = (int64_t)time(NULL) - 60;
        pdkpass_ui_results_update(0);
        assert(find_label(lv_screen_active(), "SYNC ABOUT +30 MIN"));
        sample->session_end_utc = 0;
        pdkpass_ui_results_update(0);
        assert(find_label(lv_screen_active(), "CHECKING SESSION TIME"));
        assert(!find_label(lv_screen_active(), "RESULT PENDING"));
        check_memory();
        pdkpass_network_update_t offline = {
            .state = PDKPASS_NETWORK_OFFLINE, .time_valid = true,
        };
        sample->session_end_utc = (int64_t)time(NULL) - 3600;
        pdkpass_ui_network_update(&offline);
        assert(find_label(lv_screen_active(), "CONNECT TO UPDATE"));
        offline.state = PDKPASS_NETWORK_SYNCING; offline.time_valid = false;
        pdkpass_ui_network_update(&offline);
        assert(find_label(lv_screen_active(), "SYNC CLOCK"));
        sample->status = PDKPASS_RESULT_READY;
        simulator_set_network(PDKPASS_NETWORK_ONLINE);
        // Offline format is available before metadata/results. Verify both
        // directions through a normal weekend, then switch to a sprint round.
        s_simulator_online = NO; // Keep fixture navigation independent of HTTP.
        for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++)
            s_results[0][i].status = PDKPASS_RESULT_UNKNOWN;
        pdkpass_ui_results_update(0);
        const pdkpass_session_kind_t normal[] = {
            PDKPASS_SESSION_FP1, PDKPASS_SESSION_FP2, PDKPASS_SESSION_FP3,
            PDKPASS_SESSION_QUALIFYING, PDKPASS_SESSION_RACE,
        };
        for (unsigned i = 0; i < 5; i++) {
            assert(find_label(lv_screen_active(), pdkpass_session_label(normal[i])));
            assert(!find_label(lv_screen_active(), "NO SESSION"));
            simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        }
        assert(find_label(lv_screen_active(), "FP1"));
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK);
        assert(find_label(lv_screen_active(), "RACE"));
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK); // detail R1 -> R2
        for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++)
            s_results[1][i].status = PDKPASS_RESULT_UNKNOWN;
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
        const pdkpass_session_kind_t sprint[] = {
            PDKPASS_SESSION_FP1, PDKPASS_SESSION_SPRINT_QUALIFYING,
            PDKPASS_SESSION_SPRINT, PDKPASS_SESSION_QUALIFYING, PDKPASS_SESSION_RACE,
        };
        for (unsigned i = 0; i < 5; i++) {
            assert(find_label(lv_screen_active(), pdkpass_session_label(sprint[i])));
            simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        }
        // Incoming metadata supersedes the offline format and can remove the
        // current selection without leaving a blank/nonexistent session page.
        for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++)
            s_results[1][i].status = PDKPASS_RESULT_NOT_HELD;
        for (unsigned i = 0; i < 5; i++)
            s_results[1][normal[i]].status = PDKPASS_RESULT_SCHEDULED;
        pdkpass_ui_results_update(1);
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        assert(find_label(lv_screen_active(), "FP2"));
        s_results[1][PDKPASS_SESSION_FP2].status = PDKPASS_RESULT_CANCELLED;
        pdkpass_ui_results_update(1);
        assert(find_label(lv_screen_active(), "SESSION CANCELLED"));
        s_results[1][PDKPASS_SESSION_FP2].status = PDKPASS_RESULT_NOT_HELD;
        pdkpass_ui_results_update(1);
        assert(find_label(lv_screen_active(), "FP3"));
        for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++)
            s_results[1][i].status = PDKPASS_RESULT_NOT_HELD;
        pdkpass_ui_results_update(1);
        assert(find_label(lv_screen_active(), "NO SESSIONS"));
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        assert(find_label(lv_screen_active(), "NO SESSIONS"));
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
        assert(!find_label(lv_screen_active(), "NO SESSION")); // no absent detail rows
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK); // detail R2 -> R1
        for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++)
            s_results[0][i].status = PDKPASS_RESULT_NOT_HELD;
        sample->status = PDKPASS_RESULT_READY;
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
        assert(find_label(lv_screen_active(), "MAX VERSTAPPEN"));
        s_simulator_online = YES;
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // results -> detail
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // detail -> home
        s_flush_pixels = 0; s_flush_calls = 0;
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK); // home -> calendar
        s_flush_pixels = 0; s_flush_calls = 0;
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK); // same calendar page
        printf("redraw calendar row: %zu pixels, %u flushes\n",
               s_flush_pixels, s_flush_calls);
        assert(s_flush_pixels > 0);
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // calendar -> home
        s_flush_pixels = 0; s_flush_calls = 0;
        pdkpass_ui_battery_update(88);
        simulator_refresh();
        printf("redraw unchanged battery: %zu pixels, %u flushes\n",
               s_flush_pixels, s_flush_calls);
        assert(s_flush_pixels == 0 && s_flush_calls == 0);
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK); // home -> standings
        s_flush_pixels = 0; s_flush_calls = 0;
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK); // same standings page
        printf("redraw standings row: %zu pixels, %u flushes\n",
               s_flush_pixels, s_flush_calls);
        assert(s_flush_pixels > 0 && s_flush_pixels < 240U * 320U / 2U);
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // standings -> home
        s_flush_pixels = 0; s_flush_calls = 0;
        simulator_set_network(PDKPASS_NETWORK_ONLINE); // duplicate status
        printf("redraw unchanged network: %zu pixels, %u flushes\n",
               s_flush_pixels, s_flush_calls);
        assert(s_flush_pixels == 0 && s_flush_calls == 0);
        simulator_set_network(PDKPASS_NETWORK_OFFLINE);
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // home -> network
        s_flush_pixels = 0; s_flush_calls = 0;
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK); // network selection
        printf("redraw network selection: %zu pixels, %u flushes\n",
               s_flush_pixels, s_flush_calls);
        assert(s_flush_pixels > 0 && s_flush_pixels < 240U * 320U / 2U);
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK); // restore first item
        check_memory();
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // manual setup
        assert(find_label(lv_screen_active(), "SCAN TO CONNECT"));
        assert(find_label(lv_screen_active(), "OK:INFO  HOLD:BACK"));
        assert(!find_label(lv_screen_active(), "CONNECT PHONE TO"));
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // QR -> manual info
        assert(find_label(lv_screen_active(), "CONNECT PHONE TO"));
        assert(find_label(lv_screen_active(), "OK:SCAN  HOLD:BACK"));
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // manual info -> QR
        assert(find_label(lv_screen_active(), "SCAN TO CONNECT"));
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // stop hotspot
        assert(!find_label(lv_screen_active(), "SCAN TO CONNECT"));
        assert(find_label(lv_screen_active(), "WI-FI SETUP"));
        check_memory();
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK);
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // retry saved only
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // cancel retry
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // home
        check_memory();
        const int levels[] = {-1, 0, 1, 15, 20, 21, 100};
        for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
            pdkpass_ui_battery_update(levels[i]);
            check_memory();
        }
        simulator_set_network(PDKPASS_NETWORK_SETUP);
        check_memory();
        const char *errors[] = {"AUTH FAILED / RETRY", "WIFI NOT FOUND",
            "WIFI SECURITY ERROR", "CONNECTION TIMEOUT", "IP ADDRESS TIMEOUT",
            "SAVE FAILED / RETRY", "START FAILED / RETRY"};
        for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
            pdkpass_network_update_t update = {
                .state = PDKPASS_NETWORK_SETUP,
                .setup_ssid = "PDKPASS-SETUP", .setup_password = "K7M9P2X4",
                .setup_error = errors[i],
            };
            pdkpass_ui_network_update(&update);
            check_memory();
        }
        simulator_set_network(PDKPASS_NETWORK_OFFLINE);
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK);
        for (unsigned i = 0; i < 100; ++i) {
            simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
            check_memory();
        }
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        for (unsigned i = 0; i < 2 * PDKPASS_MAX_RACES; ++i) {
            simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
            check_memory();
            simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
            for (unsigned j = 0; j < 6; ++j) {
                simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
                check_memory();
            }
            simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
            simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
            simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
            check_memory();
        }
        lv_tick_inc(90001);
        lv_timer_handler();
        assert(pdkpass_ui_display_dark());
        assert(s_panel_sleeping);
        assert(!lv_display_is_invalidation_enabled(lv_display_get_default()));
        assert(lv_timer_get_paused(lv_display_get_refr_timer(lv_display_get_default())));
        // Background updates while dark must not restart display refreshing.
        lv_timer_t *schedule_probe = lv_timer_create(dark_timer, 1000, NULL);
        lv_timer_set_repeat_count(schedule_probe, 1);
        pdkpass_ui_battery_update(19);
        pdkpass_ui_season_update();
        unsigned dark_results = s_results_status_reads, dark_points = s_points_status_reads;
        lv_tick_inc(60000);
        lv_timer_handler();
        assert(s_results_status_reads == dark_results && s_points_status_reads == dark_points);
        assert(dark_timer_fired == 1);
        assert(!lv_display_is_invalidation_enabled(lv_display_get_default()));
        assert(lv_timer_get_paused(lv_display_get_refr_timer(lv_display_get_default())));
        s_panel_wake_fail = true;
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
        assert(pdkpass_ui_display_dark() && s_panel_sleeping);
        assert(s_results_status_reads == dark_results && s_points_status_reads == dark_points);
        assert(!lv_display_is_invalidation_enabled(lv_display_get_default()));
        s_panel_wake_fail = false;
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
        assert(!pdkpass_ui_display_dark());
        assert(s_results_status_reads > dark_results && s_points_status_reads > dark_points);
        assert(!s_panel_sleeping);
        assert(lv_display_is_invalidation_enabled(lv_display_get_default()));
        check_memory();
        // Reminder temporarily wakes a dark screen and returns to sleep.
        lv_tick_inc(90001);lv_timer_handler();
        assert(pdkpass_ui_display_dark());
        pdkpass_reminder_entry_t alert={.round=15,.kind=PDKPASS_SESSION_SPRINT_QUALIFYING,
            .session_key=777,.start_utc=(int64_t)time(NULL)+600};
        pdkpass_ui_reminder_show(&alert);
        assert(!pdkpass_ui_display_dark()&&!s_panel_sleeping&&s_reminder_sound_playing);
        assert(find_label(lv_screen_active(),"SPRINT QUALIFYING"));
        assert(find_label(lv_screen_active(),"ANY KEY TO DISMISS"));
        check_memory();
        pdkpass_ui_reminder_dismiss();
        assert(pdkpass_ui_display_dark()&&s_panel_sleeping&&!s_reminder_sound_playing);
        // Automatic dismissal after 15 seconds also restores dark sleep.
        pdkpass_ui_reminder_show(&alert);
        lv_tick_inc(15001);lv_timer_handler();
        assert(pdkpass_ui_display_dark()&&!s_reminder_sound_playing);
        // Wake failure still shows no pixels and the reminder has bounded life.
        s_panel_wake_fail=true;pdkpass_ui_reminder_show(&alert);
        assert(pdkpass_ui_display_dark()&&s_reminder_sound_playing);
        pdkpass_ui_reminder_dismiss();s_panel_wake_fail=false;
        simulator_send_button(BSP_BTN_OK,BSP_BTN_CLICK);
        assert(!pdkpass_ui_display_dark());
        pdkpass_ui_reminder_show(&alert);
        simulator_send_button(BSP_BTN_OK,BSP_BTN_LONG);
        assert(!s_reminder_sound_playing&&!find_label(lv_screen_active(),"ANY KEY TO DISMISS"));
        check_memory();
        // Persistent toggle in the four-row network menu remains readable.
        simulator_send_button(BSP_BTN_OK,BSP_BTN_LONG);
        assert(find_label(lv_screen_active(),"ALERTS: ON"));
        const char *sync_rows[] = {"CAL SYNC NEVER", "RESULTS NEVER", "DRIVERS CACHE DATE?", "TEAMS CACHE DATE?"};
        for (unsigned row = 0; row < 4; row++) {
            lv_obj_t *label = find_label(lv_screen_active(), sync_rows[row]);
            assert(label && lv_obj_get_y(label) == (int)row * 14);
        }
        lv_obj_t *back = lv_obj_get_parent(find_label(lv_screen_active(), "BACK"));
        assert(lv_obj_get_y(back) + lv_obj_get_height(back) <= 177);
        for(unsigned i=0;i<4;i++) {
            lv_obj_t *label=find_label(lv_screen_active(),"ALERTS: ON");
            assert(label);
            if(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(label),0),
                           lv_color_hex(0xFFD928))) break;
            simulator_send_button(BSP_BTN_DOWN,BSP_BTN_CLICK);
        }
        simulator_send_button(BSP_BTN_OK,BSP_BTN_CLICK);
        assert(!pdkpass_reminder_enabled());
        assert(find_label(lv_screen_active(),"ALERTS: OFF"));
        check_memory();
        simulator_send_button(BSP_BTN_OK,BSP_BTN_CLICK);
        assert(pdkpass_reminder_enabled());
        // The fourth menu action returns directly home.
        simulator_send_button(BSP_BTN_DOWN,BSP_BTN_CLICK);
        simulator_send_button(BSP_BTN_OK,BSP_BTN_CLICK);
        assert(!find_label(lv_screen_active(), "ALERTS: ON"));
        check_memory();
        lv_mem_monitor_t memory;
        lv_mem_monitor(&memory);
        printf("UI memory PASS: configured=%u usable=%zu peak=%zu largest=%zu\n",
               (unsigned)LV_MEM_SIZE, memory.total_size, memory.max_used,
               memory.free_biggest_size);
    }
}
