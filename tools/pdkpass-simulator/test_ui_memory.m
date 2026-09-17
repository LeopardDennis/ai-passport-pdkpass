// Exercise production widgets and transformed text within the firmware pool.
#define main simulator_application_main
#include "simulator.m"
#undef main
#include <assert.h>

static void check_body_text(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const lv_font_t *font = lv_obj_get_style_text_font(obj, 0);
        if (font->line_height == 14) {
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(obj), font, 0, 0,
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
        simulator_initialize();
        simulator_set_network(PDKPASS_NETWORK_OFFLINE);
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // home -> network
        check_memory();
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK); // manual setup
        check_memory();
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG); // stop hotspot
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
        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
        check_memory();
        lv_mem_monitor_t memory;
        lv_mem_monitor(&memory);
        printf("UI memory PASS: configured=%u usable=%zu peak=%zu largest=%zu\n",
               (unsigned)LV_MEM_SIZE, memory.total_size, memory.max_used,
               memory.free_biggest_size);
    }
}
