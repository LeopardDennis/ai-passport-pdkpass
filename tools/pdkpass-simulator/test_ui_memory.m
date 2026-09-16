// Exercise production widgets and transformed text within the firmware pool.
#define main simulator_application_main
#include "simulator.m"
#undef main
#include <assert.h>

static void check_memory(void)
{
    simulator_refresh();
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
        const int levels[] = {-1, 0, 1, 15, 20, 21, 100};
        for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
            pdkpass_ui_battery_update(levels[i]);
            check_memory();
        }
        simulator_set_network(PDKPASS_NETWORK_SETUP);
        check_memory();
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
