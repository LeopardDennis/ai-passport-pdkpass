#include "pdkpass_ui.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "pdkpass_data.h"
#include "pdkpass_font.h"
#include "pdkpass_power.h"
#include "pdkpass_model.h"
#include "pdkpass_results.h"
#include "pdkpass_schedule.h"
#include "pdkpass_season_core.h"
#include "pdkpass_season.h"
#include "pdkpass_sync_policy.h"
#include "pdkpass_theme.h"
#include "pdkpass_tracks.h"
#include "ui_pixel.h"
#include "ui_pixel_math.h"
#include "lvgl.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CONTENT_X 8
#define CONTENT_Y 82
#define CONTENT_W 224
#define CONTENT_H 191
#define STATUS_X 8
#define STATUS_Y 52
#define STATUS_W 224
#define STATUS_H 23
#define FOOTER_X 8
#define FOOTER_Y 281
#define FOOTER_W 224
#define FOOTER_H 32
#define INNER_W 210
#define INNER_H 177
#define CALENDAR_ROWS 5
#define STANDINGS_ROWS 5
#define IDLE_DIM_SECONDS 30
#define IDLE_OFF_SECONDS 90
#define CLOCK_FALLBACK_PERIOD_MS 86400000U
#define BEIJING_OFFSET_SECONDS 28800LL

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_content;
static lv_obj_t *s_hint_box;
static lv_obj_t *s_hint;
static lv_obj_t *s_battery;
static lv_obj_t *s_battery_fill;
static lv_obj_t *s_battery_tip;
static int s_battery_soc = -1;
static lv_obj_t *s_network;
static lv_obj_t *s_status_left;
static lv_obj_t *s_status_right;

static lv_timer_t *s_idle_timer;
static lv_timer_t *s_clock_timer;
static pdkpass_state_t s_state;
static pdkpass_season_snapshot_t s_season;

static bool s_time_valid;
static bool s_time_estimated;
static uint32_t s_last_activity;
static bool s_needs_render;
static int s_list_page = -1;
static size_t s_list_start;
static size_t s_list_selected = (size_t)-1;
static lv_obj_t *s_list_rows[STANDINGS_ROWS];
static lv_obj_t *s_list_footer;
static lv_obj_t *s_network_cards[3];
static unsigned s_idle_stage;
static uint32_t s_status_background = UI_SKY;
static uint32_t s_battery_background = UINT32_MAX;
static pdkpass_network_state_t s_network_state = PDKPASS_NETWORK_STARTING;
static char s_setup_ssid[33];
static char s_setup_password[16];
static char s_setup_error[32];
static unsigned s_setup_seconds_left;
static bool s_hotspot_active;
static lv_obj_t *s_setup_countdown;
static lv_point_precise_t s_track_points[49];

static lv_obj_t *make_block(lv_obj_t *parent, int x, int y, int w, int h,
                            uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t color, int border)
{
    lv_obj_t *card = make_block(parent, x, y, w, h, color);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(card, border, 0);
    return card;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            int width, const lv_font_t *font, uint32_t color)
{
    if (font == &lv_font_unscii_8) {
        font = &pdkpass_body_font;
        y -= 2;
    }
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *make_center_label(lv_obj_t *parent, const char *text,
                                   int x, int y, int width,
                                   const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = make_label(parent, text, x + 3, y, width - 6, font, color);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static lv_obj_t *make_zoom_label(lv_obj_t *parent, const char *text,
                                 int x, int y, int width, uint32_t color)
{
    int logical_width = width / 2;
    lv_obj_t *label = make_center_label(parent, text,
        x + (width - logical_width) / 2, y + 8, logical_width,
        &lv_font_unscii_16, color);
    lv_obj_set_style_transform_pivot_x(label, logical_width / 2, 0);
    lv_obj_set_style_transform_pivot_y(label, 8, 0);
    lv_obj_set_style_transform_scale(label, 512, 0);
    return label;
}

static lv_obj_t *make_medium_label(lv_obj_t *parent, const char *text,
                                   int x, int y, int width, uint32_t color)
{
    x += 3;
    width -= 6;
    lv_point_t size;
    lv_text_get_size(&size, text, &pdkpass_body_font, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int scale = size.x > 0 && size.x * 282 > width * 256
                          ? width * 256 / size.x : 282;
    int logical_width = width * 256 / scale + 6;
    lv_obj_t *label = make_center_label(parent, text,
        x + (width - logical_width) / 2, y + 8, logical_width,
        &lv_font_unscii_8, color);
    lv_obj_set_style_transform_pivot_x(label, logical_width / 2, 0);
    lv_obj_set_style_transform_scale_x(label, scale, 0);
    return label;
}

static lv_obj_t *make_fit_zoom_label(lv_obj_t *parent, const char *text,
                                     int x, int y, int width, uint32_t color)
{
    const int margin = 8;
    const int available_width = width - margin;
    lv_point_t text_size;
    lv_text_get_size(&text_size, text, &lv_font_unscii_16, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int scale = ui_pixel_fit_scale(text_size.x, available_width, 512);
    if (scale >= 512) {
        return make_zoom_label(parent, text, x, y, width, color);
    }
    if (scale >= 256) {
        // Keep the title at its fitted size instead of dropping straight from
        // double-size to the unscaled font for medium-length race names.
        int logical_width = available_width * 256 / scale;
        lv_obj_t *label = make_label(parent, text,
            x + (width - logical_width) / 2, y + 8, logical_width,
            &lv_font_unscii_16, color);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_transform_pivot_x(label, logical_width / 2, 0);
        lv_obj_set_style_transform_pivot_y(label, 8, 0);
        lv_obj_set_style_transform_scale(label, scale, 0);
        return label;
    }
    return make_medium_label(parent, text, x, y, width, color);
}

static uint32_t contrast_color(uint32_t color)
{
    unsigned r = (color >> 16) & 0xff;
    unsigned g = (color >> 8) & 0xff;
    unsigned b = color & 0xff;
    return r * 299 + g * 587 + b * 114 > 150000 ? UI_INK : 0xFFFFFF;
}

static uint32_t result_driver_accent(const pdkpass_podium_driver_t *result,
                                     uint32_t fallback)
{
    if (!result) return fallback;
    for (size_t i = 0; i < s_season.driver_count; i++) {
        const pdkpass_driver_t *driver = &s_season.drivers[i];
        if (strncmp(driver->code, result->code, sizeof(driver->code)) == 0) {
            return driver->accent;
        }
    }
    for (size_t i = 0; i < s_season.driver_count; i++) {
        const pdkpass_driver_t *driver = &s_season.drivers[i];
        if (result->team[0] != '\0' && strcmp(driver->team, result->team) == 0) {
            return driver->accent;
        }
    }
    return fallback;
}

static void set_gradient(lv_obj_t *obj, uint32_t top, uint32_t bottom)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(top), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(bottom), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
}

static void content_reset(uint32_t top, uint32_t bottom)
{
    s_setup_countdown = NULL;
    s_list_page = -1;
    s_list_selected = (size_t)-1;
    memset(s_list_rows, 0, sizeof(s_list_rows));
    s_list_footer = NULL;
    memset(s_network_cards, 0, sizeof(s_network_cards));
    lv_obj_clean(s_content);
    set_gradient(s_content, top, bottom);
}

static pdkpass_theme_t theme_for_race(const pdkpass_race_t *race)
{
    pdkpass_theme_t theme = {
        .top = UI_SKY,
        .bottom = UI_SKY_DARK,
    };
    if (race) pdkpass_theme_get(race->circuit, &theme);
    return theme;
}

static const char *detail_session_short_label(pdkpass_session_kind_t session)
{
    switch (session) {
    case PDKPASS_SESSION_FP1:
        return "FP1";
    case PDKPASS_SESSION_SPRINT_QUALIFYING:
        return "SPR Q";
    case PDKPASS_SESSION_QUALIFYING:
        return "QUALI";
    case PDKPASS_SESSION_RACE:
        return "RACE";
    default:
        return pdkpass_session_label(session);
    }
}

static pdkpass_session_kind_t detail_middle_session(const pdkpass_race_t *race)
{
    return race && strncmp(race->session_two_cn, "SPR Q", 5U) == 0
               ? PDKPASS_SESSION_SPRINT_QUALIFYING
               : PDKPASS_SESSION_QUALIFYING;
}

static void detail_session_line(size_t race_index,
                                pdkpass_session_kind_t session,
                                const char *schedule, char *output,
                                size_t capacity)
{
    if (!output || capacity == 0U) return;
    snprintf(output, capacity, "%s", schedule ? schedule : "SCHEDULE PENDING");

    pdkpass_result_snapshot_t result;
    bool available = pdkpass_results_get(race_index, session, &result);
    if (available && result.status == PDKPASS_RESULT_READY) {
        snprintf(output, capacity, "%s RESULT",
                 detail_session_short_label(session));
    } else if (available && result.status == PDKPASS_RESULT_CANCELLED) {
        snprintf(output, capacity, "%s CANCELLED",
                 detail_session_short_label(session));
    } else if (available && result.status == PDKPASS_RESULT_NOT_HELD) {
        snprintf(output, capacity, "NO SESSION");
    } else if (s_time_valid && available && result.session_end_utc > 0 &&
               (int64_t)time(NULL) >= result.session_end_utc) {
        snprintf(output, capacity, "%s PENDING",
                 detail_session_short_label(session));
    } else if (s_time_valid && race_index < s_season.race_count &&
               (int64_t)time(NULL) >=
                   s_season.races[race_index].switch_at_utc) {
        // Before session discovery completes, an entirely historical weekend
        // is already known to be over, so its rows should not look upcoming.
        snprintf(output, capacity, "%s PENDING",
                 detail_session_short_label(session));
    }
}

static void set_title(const char *text)
{
    ui_pixel_screen_set_title(s_screen, text);
}

static void set_status(const char *text, uint32_t background)
{
    if (s_status_background == background &&
        strcmp(lv_label_get_text(s_network), text) == 0) return;
    s_status_background = background;
    set_gradient(s_status, background,
                 background == UI_SKY ? UI_SKY_DARK : background);
    lv_label_set_text(s_network, text);
    lv_obj_set_style_text_color(s_network,
        lv_color_hex(contrast_color(background)), 0);
    pdkpass_ui_battery_update(s_battery_soc);
    lv_obj_add_flag(s_status_left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_status_right, LV_OBJ_FLAG_HIDDEN);
}

static void show_status_flags(void)
{
    // The right-hand status area now carries the battery reading.
    lv_obj_add_flag(s_status_left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_status_right, LV_OBJ_FLAG_HIDDEN);
}

static void set_hint(const char *text)
{
    lv_label_set_text(s_hint, text);
}

static void title_for_season(char *output, size_t capacity,
                             const char *prefix)
{
    snprintf(output, capacity, "%s %02u", prefix,
             (unsigned)(s_season.year % 100U));
}

static void render_wifi_setup(void)
{
    set_title("PDKPASS WIFI");
    set_status(s_network_state == PDKPASS_NETWORK_CONNECTING ? "CONNECTING" : "SETUP MODE", UI_RED);
    content_reset(UI_PAPER, 0xE6E7DA);

    make_center_label(s_content, "CONNECT PHONE TO", 0, 7, INNER_W,
                      &lv_font_unscii_8, UI_SKY_DARK);
    lv_obj_t *ssid = make_card(s_content, 5, 24, 200, 34, UI_SKY, 3);
    make_medium_label(ssid, s_setup_ssid, 0, 2, 194, 0xFFFFFF);
    make_center_label(s_content, "PASSWORD", 0, 67, INNER_W,
                      &lv_font_unscii_8, UI_SKY_DARK);
    lv_obj_t *password = make_card(s_content, 5, 81, 200, 29, UI_YELLOW, 3);
    make_medium_label(password, s_setup_password, 0, 0, 194, UI_INK);
    make_center_label(s_content, s_setup_error[0] ? s_setup_error : "OPEN IN BROWSER", 0, 121, INNER_W,
                      &lv_font_unscii_8, s_setup_error[0] ? UI_RED : UI_SKY_DARK);
    make_center_label(s_content, PDKPASS_SETUP_IP, 0, 140, INNER_W,
                      &lv_font_unscii_16, UI_INK);
    char remaining[28];
    snprintf(remaining, sizeof(remaining), "AUTO OFF %02u:%02u",
             s_setup_seconds_left / 60U, s_setup_seconds_left % 60U);
    s_setup_countdown = make_center_label(s_content, remaining, 0, 161, INNER_W,
                      &lv_font_unscii_8, UI_RED);
    set_hint("HOLD OK: CLOSE WI-FI");
}

static void format_sync_line(pdkpass_sync_service_t service, unsigned season_year,
                             int64_t last, bool cached, char *line, size_t size)
{
    const char *label = service == PDKPASS_SYNC_SEASON ? "CAL SYNC" : "RESULTS";
    bool other_season = false;
    if (last > 0) {
        time_t local = (time_t)(last + BEIJING_OFFSET_SECONDS);
        struct tm parts;
        gmtime_r(&local, &parts);
        unsigned date_year = (unsigned)(parts.tm_year + 1900);
        other_season = season_year != 0U && date_year != season_year;
        if (!other_season) {
            snprintf(line, size, "%s %02u.%02d.%02d", label,
                     date_year % 100U, parts.tm_mon + 1, parts.tm_mday);
            return;
        }
    }
    if (cached) {
        snprintf(line, size, "%s CACHE DATE?",
                 service == PDKPASS_SYNC_SEASON ? "CAL" : "RESULT");
    } else if (other_season) {
        snprintf(line, size, "%s %u NO SYNC",
                 service == PDKPASS_SYNC_SEASON ? "CAL" : "RESULT",
                 season_year);
    } else {
        snprintf(line, size, "%s NEVER", label);
    }
}

static void render_network_menu(void)
{
    set_title("NETWORK");
    set_status(s_network_state == PDKPASS_NETWORK_ONLINE ? "WI-FI CONNECTED" : "WI-FI OPTIONS", UI_SKY);
    content_reset(UI_SKY, UI_SKY_DARK);
    for (unsigned i = 0; i < PDKPASS_SYNC_COUNT; i++) {
        pdkpass_sync_service_t service = (pdkpass_sync_service_t)i;
        bool cached = service == PDKPASS_SYNC_SEASON
                          ? pdkpass_season_has_cached_data()
                          : pdkpass_results_has_cached_data();
        char line[32];
        format_sync_line(service, s_season.year,
                         pdkpass_sync_last_success(service), cached,
                         line, sizeof(line));
        make_center_label(s_content, line, 0, (int)i * 15, INNER_W,
                          &lv_font_unscii_8, UI_PAPER);
    }
    const char *titles[] = {"RETRY WI-FI", "WI-FI SETUP", "BACK"};
    const char *subtitles[] = {"SAVED WI-FI ONLY", "TEMPORARY HOTSPOT", "RETURN TO HOME"};
    for (unsigned i = 0; i < 3U; i++) {
        bool selected = s_state.network_selection == i;
        uint32_t ink = selected ? UI_INK : UI_SKY_DARK;
        lv_obj_t *card = make_card(s_content, 5, 34 + (int)i * 45, 200, 42,
                                   selected ? UI_YELLOW : UI_PAPER, 2);
        s_network_cards[i] = card;
        make_center_label(card, titles[i], 0, 2, 194, &lv_font_unscii_16, ink);
        make_center_label(card, subtitles[i], 0, 23, 194, &lv_font_unscii_8, ink);
    }
    set_hint("UP/DN OK  HOLD:BACK");
}

static bool update_network_selection(unsigned previous, unsigned selected)
{
    if (previous >= 3U || selected >= 3U ||
        !s_network_cards[previous] || !s_network_cards[selected]) return false;
    const unsigned rows[] = {previous, selected};
    for (size_t i = 0; i < 2U; i++) {
        unsigned row = rows[i];
        bool active = row == selected;
        lv_obj_t *card = s_network_cards[row];
        lv_obj_set_style_bg_color(card,
            lv_color_hex(active ? UI_YELLOW : UI_PAPER), 0);
        uint32_t ink = active ? UI_INK : UI_SKY_DARK;
        for (uint32_t child = 0; child < lv_obj_get_child_count(card); child++)
            lv_obj_set_style_text_color(lv_obj_get_child(card, child),
                                        lv_color_hex(ink), 0);
    }
    return true;
}

static void render_network_progress(void)
{
    if (s_hotspot_active) { render_wifi_setup(); return; }
    set_title("WI-FI");
    set_status("SAVED NETWORKS", UI_SKY);
    content_reset(UI_SKY, UI_SKY_DARK);
    bool busy = s_network_state == PDKPASS_NETWORK_CONNECTING;
    make_center_label(s_content, busy ? "SEARCHING" : "NOT CONNECTED", 0, 30,
                      INNER_W, &lv_font_unscii_16, UI_PAPER);
    make_center_label(s_content, busy ? "TRYING SAVED WI-FI" : s_setup_error,
                      0, 76, INNER_W, &lv_font_unscii_8, UI_PAPER);
    make_center_label(s_content, "HOTSPOT IS OFF", 0, 120, INNER_W,
                      &lv_font_unscii_8, UI_YELLOW);
    set_hint(busy ? "HOLD OK CANCEL" : "OK / HOLD: MENU");
}

static void render_network_confirm(void)
{
    set_title("WI-FI SETUP");
    set_status("CONFIRM", UI_YELLOW);
    content_reset(UI_PAPER, 0xE6E7DA);
    make_center_label(s_content, "ADD NETWORK?", 0, 25, INNER_W, &lv_font_unscii_16, UI_INK);
    make_center_label(s_content, "CURRENT CONNECTION", 0, 70, INNER_W, &lv_font_unscii_8, UI_INK);
    make_center_label(s_content, "MAY BE INTERRUPTED", 0, 89, INNER_W, &lv_font_unscii_8, UI_INK);
    make_center_label(s_content, "HOTSPOT: MAX 10 MIN", 0, 130, INNER_W, &lv_font_unscii_8, UI_RED);
    set_hint("OK:NEXT  HOLD:CANCEL");
}

static void render_season_complete(void)
{
    char title[20];
    title_for_season(title, sizeof(title), "PDKPASS");
    set_title(title);
    set_status("FINAL FLAG", UI_RED);
    content_reset(UI_SKY, UI_SKY_DARK);

    make_zoom_label(s_content, "SEASON", 0, 12, INNER_W, 0xFFFFFF);
    make_medium_label(s_content, "COMPLETE", 0, 55, INNER_W, UI_YELLOW);
    make_center_label(s_content, "THE FINAL FLAG IS OUT", 0, 103, INNER_W,
                      &lv_font_unscii_16, 0xFFFFFF);
    lv_obj_t *offline = make_card(s_content, 8, 133, 194, 31,
                                  UI_PAPER, 3);
    make_center_label(offline, "SEASON DATA SAVED", 0, 7, 188,
                      &lv_font_unscii_8, UI_INK);
    set_hint("UP/DN:RACE HOLD:VIEWS");
}

static const char *network_word(void)
{
    switch (s_network_state) {
    case PDKPASS_NETWORK_SETUP: return "SETUP";
    case PDKPASS_NETWORK_CONNECTING: return "WIFI...";
    case PDKPASS_NETWORK_SYNCING: return "TIME...";
    case PDKPASS_NETWORK_ONLINE: return "WIFI OK";
    case PDKPASS_NETWORK_OFFLINE: return "WIFI OFF";
    case PDKPASS_NETWORK_TIME_ERROR: return "NTP ERR";
    default: return "NET...";
    }
}

static void update_home_status(void)
{
    char text[32];
    if (s_time_valid) {
        time_t local = (time_t)((int64_t)time(NULL) +
                                BEIJING_OFFSET_SECONDS);
        struct tm parts;
        gmtime_r(&local, &parts);
        snprintf(text, sizeof(text), "%s | %02d.%02d", network_word(),
                 parts.tm_mon + 1, parts.tm_mday);
    } else if (s_time_estimated) {
        time_t local = (time_t)((int64_t)time(NULL) +
                                BEIJING_OFFSET_SECONDS);
        struct tm parts;
        gmtime_r(&local, &parts);
        snprintf(text, sizeof(text), "%s | ~%02d.%02d", network_word(),
                 parts.tm_mon + 1, parts.tm_mday);
    } else {
        snprintf(text, sizeof(text), "%s | --.--", network_word());
    }
    uint32_t background = UI_SKY;
    if (s_network_state == PDKPASS_NETWORK_OFFLINE) {
        background = UI_RED;
    } else if ((!s_state.season_complete || s_state.home_browsing) &&
               s_state.selected_race < s_season.race_count) {
        background = theme_for_race(&s_season.races[s_state.selected_race]).top;
    }
    set_status(text, background);
}

static void make_progress(size_t current, size_t total)
{
    const int segments = 6;
    const int gap = 3;
    const int width = (184 - gap * (segments - 1)) / segments;
    int filled = total > 0U ? (int)(((current + 1U) * segments + total - 1U) /
                                   total) : 0;
    int x = 13;
    for (int i = 0; i < segments; i++) {
        uint32_t color = i < filled ? (i == 0 ? UI_RED : UI_YELLOW)
                                    : UI_PAPER;
        make_card(s_content, x, 163, width, 9, color, 2);
        x += width + gap;
    }
}

static void render_home(void)
{
    if (s_network_state == PDKPASS_NETWORK_SETUP) {
        render_wifi_setup();
        return;
    }
    if ((s_state.season_complete && !s_state.home_browsing) ||
        s_state.selected_race >= s_season.race_count) {
        render_season_complete();
        return;
    }

    const pdkpass_race_t *race = &s_season.races[s_state.selected_race];
    pdkpass_theme_t theme = theme_for_race(race);
    char title[20];
    title_for_season(title, sizeof(title), "PDKPASS");
    set_title(title);
    update_home_status();
    ui_pixel_screen_set_theme(s_screen, theme.top, theme.bottom);
    content_reset(theme.top, theme.bottom);

    char round[8];
    snprintf(round, sizeof(round), "R%u", race->round);
    make_zoom_label(s_content, round, 0, 3, INNER_W, 0xFFFFFF);
    make_fit_zoom_label(s_content, race->country, 0, 35, INNER_W, UI_PAPER);
    make_center_label(s_content, race->circuit, 0, 73, INNER_W,
                      &lv_font_unscii_16, 0xFFFFFF);
    char weekend[20];
    if (strlen(race->weekend) == 9U && race->weekend[2] == '-' &&
        race->weekend[5] == ' ') {
        snprintf(weekend, sizeof(weekend), "%.3s %.5s",
                 race->weekend + 6, race->weekend);
    } else {
        snprintf(weekend, sizeof(weekend), "%s", race->weekend);
    }
    make_center_label(s_content, weekend, 0, 94, INNER_W,
                      &lv_font_unscii_16, UI_PAPER);

    lv_obj_t *race_card = make_card(s_content, 8, 116, 194, 29,
                                    race->accent, 3);
    char race_time[22];
    size_t race_line_length = strlen(race->race_cn);
    const char *time_text = race_line_length >= 5U
                                ? race->race_cn + race_line_length - 5U
                                : "--:--";
    if (strlen(time_text) == 5 && time_text[2] == ':' &&
        time_text[0] >= '0' && time_text[0] <= '2' &&
        time_text[1] >= '0' && time_text[1] <= '9' &&
        time_text[3] >= '0' && time_text[3] <= '5' &&
        time_text[4] >= '0' && time_text[4] <= '9')
        snprintf(race_time, sizeof(race_time), "RACE %s CST", time_text);
    else snprintf(race_time, sizeof(race_time), "RACE TIME TBD");
    make_medium_label(race_card, race_time, 0, 0, 188,
                      contrast_color(race->accent));
    char page[20];
    snprintf(page, sizeof(page), "%u / %u",
             (unsigned)(s_state.selected_race + 1U),
             (unsigned)s_season.race_count);
    make_center_label(s_content, page, 0, 146, INNER_W,
                      &lv_font_unscii_16, UI_PAPER);
    make_progress(s_state.selected_race, s_season.race_count);
    set_hint("UP/DN:RACE HOLD:VIEWS");
}

static bool update_list_selection(int page, size_t start, size_t selected,
                                   size_t count)
{
    if (s_list_page != page || s_list_start != start || !s_list_footer) return false;
    for (size_t row = 0; row < STANDINGS_ROWS; row++) {
        lv_obj_t *card = s_list_rows[row];
        if (!card || start + row >= count) continue;
        size_t index = start + row;
        if (index != selected && index != s_list_selected) continue;
        uint32_t accent = page == PDKPASS_PAGE_CALENDAR
            ? s_season.races[index].accent : s_season.drivers[index].accent;
        uint32_t bg = index == selected ? accent : UI_PAPER;
        uint32_t ink = index == selected ? contrast_color(bg) : UI_INK;
        lv_obj_set_style_bg_color(card, lv_color_hex(bg), 0);
        for (uint32_t child = 0; child < lv_obj_get_child_count(card); child++) {
            lv_obj_set_style_text_color(lv_obj_get_child(card, child), lv_color_hex(ink), 0);
        }
    }
    if (page == PDKPASS_PAGE_CALENDAR && selected < count) {
        const pdkpass_race_t *race = &s_season.races[selected];
        pdkpass_theme_t theme = theme_for_race(race);
        ui_pixel_screen_set_theme(s_screen, theme.top, theme.bottom);
        set_gradient(s_content, theme.top, theme.bottom);
        char status[28];
        snprintf(status, sizeof(status), "SEASON | %u RACES", (unsigned)count);
        set_status(status, race->accent);
        lv_label_set_text_fmt(s_list_footer, "%u / %u", (unsigned)(selected + 1U), (unsigned)count);
    } else if (selected < count) {
        lv_label_set_text(s_list_footer, s_season.drivers[selected].team);
    }
    s_list_selected = selected;
    return true;
}

static void render_calendar(void)
{
    size_t start = (s_state.selected_race / CALENDAR_ROWS) * CALENDAR_ROWS;
    if (update_list_selection(PDKPASS_PAGE_CALENDAR, start, s_state.selected_race,
                              s_season.race_count)) return;
    const pdkpass_race_t *selected = s_state.selected_race < s_season.race_count
                                         ? &s_season.races[s_state.selected_race]
                                         : NULL;
    pdkpass_theme_t theme = selected ? theme_for_race(selected)
                                     : (pdkpass_theme_t){ UI_SKY, UI_SKY_DARK };
    char title[20];
    title_for_season(title, sizeof(title), "CALENDAR");
    set_title(title);
    char status[28];
    snprintf(status, sizeof(status), "SEASON | %u RACES",
             (unsigned)s_season.race_count);
    set_status(status, selected ? selected->accent : theme.top);
    ui_pixel_screen_set_theme(s_screen, theme.top, theme.bottom);
    content_reset(theme.top, theme.bottom);

    make_center_label(s_content, "SEASON CALENDAR", 0, 3, INNER_W,
                      &lv_font_unscii_8, 0xFFFFFF);
    for (size_t row = 0; row < CALENDAR_ROWS; row++) {
        size_t index = start + row;
        if (index >= s_season.race_count) break;
        const pdkpass_race_t *race = &s_season.races[index];
        bool selected = index == s_state.selected_race;
        uint32_t bg = selected ? race->accent : UI_PAPER;
        uint32_t ink = selected ? contrast_color(bg) : UI_INK;
        int y = 21 + (int)row * 28;
        lv_obj_t *card = make_card(s_content, 1, y, 208, 25, bg, 2);
        s_list_rows[row] = card;
        char line[64];
        snprintf(line, sizeof(line), "%02u %s  %s", race->round,
                 race->country, race->weekend);
        lv_point_t size;
        lv_text_get_size(&size, line, &pdkpass_body_font, 0, 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        // Preserve the full weekend (including cross-month dates) on one
        // line. Fit horizontally only; keep the enlarged glyph height.
        int scale = size.x > 192 ? 192 * 256 / size.x : 256;
        lv_obj_t *label = make_label(card, line, 6, 7, size.x,
                                     &lv_font_unscii_8, ink);
        lv_obj_set_style_transform_pivot_x(label, 0, 0);
        lv_obj_set_style_transform_scale_x(label, scale, 0);
    }
    char page[20];
    snprintf(page, sizeof(page), "%u / %u",
             (unsigned)(s_state.selected_race + 1U),
             (unsigned)s_season.race_count);
    s_list_footer = make_center_label(s_content, page, 0, 165, INNER_W,
                      &lv_font_unscii_8, UI_PAPER);
    s_list_page = PDKPASS_PAGE_CALENDAR;
    s_list_start = start;
    s_list_selected = s_state.selected_race;
    set_hint("UP/DN OK  HOLD:HOME");
}

static void render_standings(void)
{
    size_t start = (s_state.selected_driver / STANDINGS_ROWS) *
                   STANDINGS_ROWS;
    if (update_list_selection(PDKPASS_PAGE_STANDINGS, start, s_state.selected_driver,
                              s_season.driver_count)) return;
    char title[20];
    title_for_season(title, sizeof(title), "STANDINGS");
    set_title(title);
    char status[32];
    snprintf(status, sizeof(status), "POINTS | %s",
             s_season.standings_as_of);
    set_status(status, UI_RED);
    content_reset(0x17202A, 0x263743);

    if (s_season.driver_count == 0U) {
        make_zoom_label(s_content, "POINTS", 0, 34, INNER_W, UI_YELLOW);
        make_center_label(s_content, "AFTER THE FIRST RACE", 0, 91,
                          INNER_W, &lv_font_unscii_8, UI_PAPER);
        set_hint("HOLD OK HOME");
        return;
    }

    for (size_t row = 0; row < STANDINGS_ROWS; row++) {
        size_t index = start + row;
        if (index >= s_season.driver_count) break;
        const pdkpass_driver_t *driver = &s_season.drivers[index];
        bool selected = index == s_state.selected_driver;
        uint32_t bg = selected ? driver->accent : UI_PAPER;
        uint32_t ink = selected ? contrast_color(bg) : UI_INK;
        int y = 5 + (int)row * 30;
        lv_obj_t *card = make_card(s_content, 1, y, 208, 28, bg, 2);
        s_list_rows[row] = card;
        char position[5];
        char points[8];
        snprintf(position, sizeof(position), "%02u", driver->position);
        if (driver->points_tenths % 10U == 0U) {
            snprintf(points, sizeof(points), "%u",
                     driver->points_tenths / 10U);
        } else {
            snprintf(points, sizeof(points), "%u.%u",
                     driver->points_tenths / 10U,
                     driver->points_tenths % 10U);
        }
        make_label(card, position, 6, 8, 22, &lv_font_unscii_8, ink);
        make_label(card, driver->name, 32, 8, 120,
                   &lv_font_unscii_8, ink);
        lv_obj_t *score = make_label(card, points, 156, 8, 42,
                                     &lv_font_unscii_8, ink);
        lv_obj_set_style_text_align(score, LV_TEXT_ALIGN_RIGHT, 0);
    }
    const pdkpass_driver_t *selected =
        &s_season.drivers[s_state.selected_driver];
    s_list_footer = make_center_label(s_content, selected->team, 0, 165, INNER_W,
                      &lv_font_unscii_8, UI_PAPER);
    s_list_page = PDKPASS_PAGE_STANDINGS;
    s_list_start = start;
    s_list_selected = s_state.selected_driver;
    set_hint("UP/DN  HOLD:HOME");
}

static size_t build_track_points(const char *circuit)
{
    static const uint8_t fallback[] = {
        37, 12, 59, 4, 109, 4, 153, 12, 168, 30,
        153, 50, 104, 57, 56, 51, 37, 31, 37, 12,
    };
    pdkpass_track_geometry_t geometry;
    if (!pdkpass_track_get(circuit, &geometry)) {
        geometry.xy = fallback;
        geometry.point_count = sizeof(fallback) / 2U;
    }
    if (geometry.point_count > sizeof(s_track_points) / sizeof(s_track_points[0])) {
        geometry.point_count = sizeof(s_track_points) / sizeof(s_track_points[0]);
    }
    for (size_t i = 0; i < geometry.point_count; i++) {
        s_track_points[i].x = geometry.xy[i * 2U];
        s_track_points[i].y = geometry.xy[i * 2U + 1U];
    }
    return geometry.point_count;
}

static void add_track_outline(lv_obj_t *parent, const char *circuit)
{
    size_t point_count = build_track_points(circuit);
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points_mutable(line, s_track_points, point_count);
    lv_obj_set_pos(line, 7, 3);
    lv_obj_set_style_line_width(line, 4, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(UI_PAPER), 0);
    lv_obj_set_style_line_rounded(line, false, 0);
}

static void render_detail(void)
{
    if (s_state.selected_race >= s_season.race_count) return;
    const pdkpass_race_t *race = &s_season.races[s_state.selected_race];
    pdkpass_theme_t theme = theme_for_race(race);
    char title[40];
    snprintf(title, sizeof(title), "%s . R%u", race->circuit, race->round);
    set_title(title);
    char status[32];
    snprintf(status, sizeof(status), "%s GP", race->country);
    set_status(status, theme.top);
    show_status_flags();
    ui_pixel_screen_set_theme(s_screen, theme.top, theme.bottom);
    content_reset(theme.top, theme.bottom);

    lv_obj_t *track = make_card(s_content, 1, 1, 208, 69, theme.top, 3);
    set_gradient(track, theme.top, theme.bottom);
    add_track_outline(track, race->circuit);

    char distance[18];
    char laps[16];
    if (race->circuit_length_m > 0U) {
        snprintf(distance, sizeof(distance), "%u.%03u KM",
                 race->circuit_length_m / 1000,
                 race->circuit_length_m % 1000);
    } else {
        snprintf(distance, sizeof(distance), "-- KM");
    }
    if (race->laps > 0U) snprintf(laps, sizeof(laps), "%u LAPS", race->laps);
    else snprintf(laps, sizeof(laps), "-- LAPS");
    lv_obj_t *distance_card = make_card(s_content, 1, 74, 101, 30,
                                        theme.bottom, 3);
    lv_obj_t *laps_card = make_card(s_content, 108, 74, 101, 30,
                                    theme.bottom, 3);
    make_medium_label(distance_card, distance, 0, 0, 95, 0xFFFFFF);
    make_medium_label(laps_card, laps, 0, 0, 95, 0xFFFFFF);

    pdkpass_session_kind_t session_kinds[] = {
        PDKPASS_SESSION_FP1,
        detail_middle_session(race),
        PDKPASS_SESSION_RACE,
    };
    const char *session_schedules[] = {
        race->session_one_cn, race->session_two_cn, race->race_cn,
    };
    for (size_t i = 0; i < 3U; i++) {
        char line[PDKPASS_SESSION_LINE_LEN];
        detail_session_line(s_state.selected_race, session_kinds[i],
                            session_schedules[i], line, sizeof(line));
        uint32_t bg = i == 2U ? UI_YELLOW : UI_PAPER;
        lv_obj_t *row = make_card(s_content, 1, 109 + (int)i * 22,
                                  208, 21, bg, 2);
        make_center_label(row, line, 1, 4, 204,
                          &lv_font_unscii_8, UI_INK);
    }
    set_hint("UP/DN OK  HOLD:BACK");
}

static void render_results(void)
{
    if (s_state.selected_race >= s_season.race_count) return;
    const pdkpass_race_t *race = &s_season.races[s_state.selected_race];
    char title[40];
    snprintf(title, sizeof(title), "%s . R%u", race->country, race->round);
    set_title(title);
    set_status(pdkpass_session_label(s_state.selected_session), race->accent);
    content_reset(0x17202A, 0x263743);

    pdkpass_result_snapshot_t result;
    bool available = pdkpass_results_get(s_state.selected_race,
                                         s_state.selected_session, &result);
    if (!available || result.status != PDKPASS_RESULT_READY) {
        const char *state = "CONNECT TO UPDATE";
        const char *detail = "NO RESULT CACHED";
        if (available && result.status == PDKPASS_RESULT_UNKNOWN &&
            s_network_state == PDKPASS_NETWORK_ONLINE) {
            // ONLINE only means Wi-Fi is connected. The results worker may be
            // waiting for the session, a retry deadline, or its HTTP turn.
            state = s_time_valid && (int64_t)time(NULL) < race->switch_at_utc
                        ? "RESULT PENDING" : "SYNC PENDING";
            detail = "OPENF1 CHECK SCHEDULED";
        } else if (available && result.status == PDKPASS_RESULT_NOT_HELD) {
            state = "NO SESSION";
            detail = "NOT ON THIS WEEKEND";
        } else if (available && result.status == PDKPASS_RESULT_SCHEDULED) {
            state = "RESULT PENDING";
            detail = "SYNC AFTER SESSION";
        } else if (available && result.status == PDKPASS_RESULT_CANCELLED) {
            state = "SESSION CANCELLED";
            detail = "NO CLASSIFICATION";
        }
        make_medium_label(s_content, state, 0, 42, INNER_W, UI_YELLOW);
        make_medium_label(s_content, detail, 0, 95, INNER_W, UI_PAPER);
        make_center_label(s_content, "SYNC ABOUT +30 MIN", 0, 139,
                          INNER_W, &lv_font_unscii_8, UI_PAPER);
    } else {
        for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
            const pdkpass_podium_driver_t *driver = &result.podium[i];
            uint32_t background = result_driver_accent(driver, race->accent);
            uint32_t ink = contrast_color(background);
            lv_obj_t *row = make_card(s_content, 2, 7 + (int)i * 55,
                                      206, 49, background, 3);
            char position[4];
            snprintf(position, sizeof(position), "P%u", driver->position);
            make_label(row, position, 7, 5, 32, &lv_font_unscii_16, ink);
            make_label(row, driver->code, 47, 9, 32,
                       &lv_font_unscii_8, ink);
            make_label(row, driver->name, 84, 5, 114,
                       &pdkpass_body_font, ink);
            make_label(row, driver->team, 45, 27, 153,
                       &lv_font_unscii_8, ink);
        }
    }
    set_hint("UP/DN SESS  OK:BACK");
}

static void render(void)
{
    if (s_idle_stage == 2) { s_needs_render = true; return; }
    s_needs_render = false;
    // The home, calendar and detail pages choose their own circuit theme.
    // Resetting them to sky first invalidates the whole screen twice.
    bool circuit_theme = s_state.page == PDKPASS_PAGE_CALENDAR ||
                         s_state.page == PDKPASS_PAGE_RACE_DETAIL ||
                         (s_state.page == PDKPASS_PAGE_HOME &&
                          s_network_state != PDKPASS_NETWORK_SETUP &&
                          (!s_state.season_complete || s_state.home_browsing));
    if (!circuit_theme)
        ui_pixel_screen_set_theme(s_screen, UI_SKY, UI_SKY_DARK);
    switch (s_state.page) {
    case PDKPASS_PAGE_NETWORK: render_network_menu(); break;
    case PDKPASS_PAGE_NETWORK_PROGRESS: render_network_progress(); break;
    case PDKPASS_PAGE_NETWORK_CONFIRM: render_network_confirm(); break;
    case PDKPASS_PAGE_HOME:
        render_home();
        break;
    case PDKPASS_PAGE_CALENDAR:
        render_calendar();
        break;
    case PDKPASS_PAGE_STANDINGS:
        render_standings();
        break;
    case PDKPASS_PAGE_RACE_DETAIL:
        render_detail();
        break;
    case PDKPASS_PAGE_RESULTS:
        render_results();
        break;
    }
}

// Tiny 3x5 digits keep the compact battery silhouette, without loading a font.
// Draw after the level so each pixel contrasts with its actual background.
static void battery_draw_digits(lv_event_t *event)
{
    int soc = s_battery_soc;
    if (soc < 0) return;
    // Keep the drawing boundary explicit, including for the target compiler.
    if (soc > 100) soc = 100;
    static const uint8_t digits[10][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7},
        {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
        {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7},
        {7, 5, 7, 1, 7},
    };
    char text[4];
    snprintf(text, sizeof(text), "%d", soc);
    int length = (int)strlen(text);
    lv_area_t body, level;
    lv_obj_get_coords(s_battery, &body);
    lv_obj_get_coords(s_battery_fill, &level);
    int left = body.x1 + (23 - (length * 4 - 1)) / 2;
    uint32_t ink = soc <= 20 ? UI_RED : contrast_color(s_status_background);
    uint32_t fill = ink;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    lv_layer_t *layer = lv_event_get_layer(event);
    for (int i = 0; i < length; ++i) {
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 3; ++x) {
                if (!(digits[text[i] - '0'][y] & (4 >> x))) continue;
                int px = left + i * 4 + x;
                int py = body.y1 + 3 + y;
                bool filled = soc > 0 && px >= level.x1 && px <= level.x2;
                rect.bg_color = lv_color_hex(filled ? contrast_color(fill) : ink);
                lv_area_t pixel = {px, py, px, py};
                lv_draw_rect(layer, &rect, &pixel);
            }
        }
    }
}

void pdkpass_ui_battery_update(int soc)
{
    if (!s_battery) return;
    int normalized = soc < 0 ? -1 : (soc > 100 ? 100 : soc);
    if (s_battery_soc == normalized &&
        s_battery_background == s_status_background) return;
    s_battery_soc = normalized;
    s_battery_background = s_status_background;
    uint32_t ink = contrast_color(s_status_background);
    uint32_t fill = s_battery_soc >= 0 && s_battery_soc <= 20 ? UI_RED : ink;
    // A light interior separates the red warning from red page themes.
    // Keep the silhouette contrasted with the status bar, including at 0%.
    bool low = s_battery_soc >= 0 && s_battery_soc <= 20;
    lv_obj_set_style_bg_color(s_battery, lv_color_hex(UI_PAPER), 0);
    lv_obj_set_style_bg_opa(s_battery, low ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_battery, lv_color_hex(ink), 0);
    lv_obj_set_style_bg_color(s_battery_tip, lv_color_hex(ink), 0);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(fill), 0);
    int width = s_battery_soc > 0 ? (19 * s_battery_soc + 99) / 100 : 0;
    lv_obj_set_width(s_battery_fill, width > 0 ? width : 1);
    if (width) lv_obj_remove_flag(s_battery_fill, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_battery_fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_battery);
}

bool pdkpass_ui_display_dark(void)
{
    return s_idle_stage == 2;
}

static void clock_tick(lv_timer_t *timer)
{
    if (!s_time_valid && !s_time_estimated) return;
    int64_t now = (int64_t)time(NULL);
    if (!s_time_valid) {
        // A restored clock can label the date, but cannot choose a new round.
        if (s_state.page == PDKPASS_PAGE_HOME) update_home_status();
        int64_t midnight = pdkpass_next_beijing_midnight(now);
        uint32_t delay_ms = midnight > now
            ? (uint32_t)(midnight - now) * 1000U : 1000U;
        lv_timer_set_period(timer ? timer : s_clock_timer, delay_ms);
        return;
    }
    size_t previous = s_state.season_complete ? s_season.race_count
                                              : s_state.home_race;
    size_t next = pdkpass_schedule_next_race(now, s_season.races,
                                             s_season.race_count);
    pdkpass_state_set_home_race(&s_state, next, s_season.race_count);
    if (s_state.page == PDKPASS_PAGE_HOME) {
        if (previous != next && !s_state.home_browsing) render();
        else update_home_status();
    }

    int64_t deadline = pdkpass_schedule_next_check(now, s_season.races,
                                                   s_season.race_count);
    uint64_t delay_ms = deadline > now ? (uint64_t)(deadline - now) * 1000U
                                       : 1000U;
    if (delay_ms > UINT32_MAX) delay_ms = UINT32_MAX;
    lv_timer_set_period(timer ? timer : s_clock_timer, (uint32_t)delay_ms);
}

static void update_network_label(void)
{
    if (s_state.page == PDKPASS_PAGE_HOME) update_home_status();
}

// The display dims after 30 seconds and turns off after 90 seconds. The next
// button event restores full brightness and is consumed as the wake gesture.
static void idle_tick(lv_timer_t *timer)
{
    uint32_t elapsed = lv_tick_get() - s_last_activity;
    if (elapsed >= IDLE_OFF_SECONDS * 1000U) {
        bsp_display_backlight(0);
        bsp_lvgl_set_drawing(false);
        pdkpass_power_display(false);
        s_idle_stage = 2;
        if (s_state.page == PDKPASS_PAGE_HOME && s_state.home_browsing) {
            pdkpass_state_reset_home_race(&s_state, s_season.race_count);
            render();
        }
        lv_timer_pause(timer);
    } else if (elapsed >= IDLE_DIM_SECONDS * 1000U) {
        bsp_display_backlight(25);
        s_idle_stage = 1;
        lv_timer_set_period(timer, IDLE_OFF_SECONDS * 1000U - elapsed);
    } else {
        lv_timer_set_period(timer, IDLE_DIM_SECONDS * 1000U - elapsed);
    }
}

void pdkpass_ui_enter(bool battery_available)
{
    (void)battery_available;
    s_last_activity = lv_tick_get();
    s_idle_stage = 0;
    pdkpass_state_init(&s_state);
    if (!pdkpass_season_snapshot(&s_season)) {
        memset(&s_season, 0, sizeof(s_season));
    }

    char title[20];
    title_for_season(title, sizeof(title), "PDKPASS");
    s_screen = ui_pixel_screen_create(title);
    s_status = ui_pixel_ticket_create(s_screen, STATUS_X, STATUS_Y,
                                      STATUS_W, STATUS_H, UI_SKY, true);
    s_network = make_center_label(s_status, "NET... | --.--", 5, 5, 180,
                           &lv_font_unscii_8, UI_PAPER);
    // Center in the gray content area only; exclude border and drop shadow.
    // The enlarged font's visible capitals need a one-pixel baseline offset
    // to share the battery's center (y=63), excluding the drop shadow.
    lv_obj_align(s_network, LV_ALIGN_LEFT_MID, 5, 1);
    s_battery = make_block(s_status, 188, 5, 23, 11, UI_SKY);
    lv_obj_set_style_radius(s_battery, 3, 0);
    lv_obj_set_style_bg_opa(s_battery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_battery, 1, 0);
    lv_obj_align(s_battery, LV_ALIGN_LEFT_MID, 188, 0);
    s_battery_fill = make_block(s_battery, 1, 1, 19, 7, UI_PAPER);
    lv_obj_set_style_radius(s_battery_fill, 1, 0);
    lv_obj_add_event_cb(s_battery, battery_draw_digits, LV_EVENT_DRAW_POST_END, NULL);
    s_battery_tip = make_block(s_status, 212, 8, 2, 5, UI_PAPER);
    lv_obj_set_style_radius(s_battery_tip, 1, 0);
    lv_obj_align(s_battery_tip, LV_ALIGN_LEFT_MID, 212, 0);
    s_status_left = make_block(s_status, 5, 5, 13, 11, 0x009246);
    lv_obj_set_style_border_color(s_status_left, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(s_status_left, 2, 0);
    lv_obj_add_flag(s_status_left, LV_OBJ_FLAG_HIDDEN);
    s_status_right = make_block(s_status, 200, 5, 13, 11, UI_RED);
    lv_obj_set_style_border_color(s_status_right, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(s_status_right, 2, 0);
    lv_obj_add_flag(s_status_right, LV_OBJ_FLAG_HIDDEN);
    s_content = ui_pixel_panel_create(s_screen, CONTENT_X, CONTENT_Y,
                                      CONTENT_W, CONTENT_H, UI_SKY);
    s_hint_box = ui_pixel_ticket_create(s_screen, FOOTER_X, FOOTER_Y,
                                        FOOTER_W, FOOTER_H, UI_PAPER, true);
    // Center the visible glyphs inside the ticket; the centered-label helper's
    // extra 3 px inset makes this full-width hint look shifted to the right.
    s_hint = make_label(s_hint_box, "", 0, 9, FOOTER_W - 6,
                        &lv_font_unscii_8, UI_INK);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    render();
    pdkpass_ui_battery_update(-1);
    s_idle_timer = lv_timer_create(idle_tick, IDLE_DIM_SECONDS * 1000U, NULL);
    s_clock_timer = lv_timer_create(clock_tick, CLOCK_FALLBACK_PERIOD_MS, NULL);
    lv_screen_load(s_screen);
}

void pdkpass_ui_network_update(const pdkpass_network_update_t *update)
{
    if (!update) return;
    pdkpass_network_state_t previous_state = s_network_state;
    bool was_hotspot = s_hotspot_active;
    s_hotspot_active = update->hotspot_active;
    s_setup_seconds_left = update->setup_seconds_left;
    s_network_state = update->state;
    s_time_valid = update->time_valid;
    s_time_estimated = update->time_estimated;
    bool error_changed = strcmp(s_setup_error, update->setup_error ? update->setup_error : "") != 0;
    snprintf(s_setup_error, sizeof(s_setup_error), "%s",
             update->setup_error ? update->setup_error : "");
    snprintf(s_setup_ssid, sizeof(s_setup_ssid), "%s",
             update->setup_ssid ? update->setup_ssid : "");
    snprintf(s_setup_password, sizeof(s_setup_password), "%s",
             update->setup_password ? update->setup_password : "");
    if (s_state.page == PDKPASS_PAGE_NETWORK_PROGRESS &&
        (update->state == PDKPASS_NETWORK_ONLINE || update->state == PDKPASS_NETWORK_SYNCING ||
         (was_hotspot && !s_hotspot_active))) s_state.page = PDKPASS_PAGE_HOME;
    if (s_setup_countdown && !error_changed)
        lv_label_set_text_fmt(s_setup_countdown, "AUTO OFF %02u:%02u",
                             s_setup_seconds_left / 60U, s_setup_seconds_left % 60U);
    update_network_label();
    clock_tick(NULL);
    if (previous_state != s_network_state || error_changed || was_hotspot != s_hotspot_active) render();
}

void pdkpass_ui_results_update(size_t race_index)
{
    if ((s_state.page == PDKPASS_PAGE_RESULTS ||
         s_state.page == PDKPASS_PAGE_RACE_DETAIL) &&
        s_state.selected_race == race_index) render();
}

void pdkpass_ui_sync_status_update(void)
{
    if (s_state.page == PDKPASS_PAGE_NETWORK) render();
}

void pdkpass_ui_season_update(void)
{
    pdkpass_season_snapshot_t updated;
    if (!pdkpass_season_snapshot(&updated)) return;
    s_season = updated;
    s_list_page = -1;
    if (s_state.selected_race >= s_season.race_count) {
        s_state.selected_race = s_season.race_count > 0U
                                    ? s_season.race_count - 1U
                                    : 0U;
    }
    if (s_state.selected_driver >= s_season.driver_count) {
        s_state.selected_driver = s_season.driver_count > 0U
                                      ? s_season.driver_count - 1U
                                      : 0U;
    }
    if (s_time_valid) {
        int64_t now = (int64_t)time(NULL);
        size_t next = pdkpass_schedule_next_race(now, s_season.races,
                                                 s_season.race_count);
        pdkpass_state_set_home_race(&s_state, next, s_season.race_count);
    }
    render();
}

void pdkpass_ui_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    bool was_off = s_idle_stage == 2;
    if (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG) {
        pdkpass_power_display(true);
        if (was_off) bsp_lvgl_set_drawing(true);
        s_last_activity = lv_tick_get();
        s_idle_stage = 0;
        lv_timer_set_period(s_idle_timer, IDLE_DIM_SECONDS * 1000U);
        lv_timer_reset(s_idle_timer);
        lv_timer_resume(s_idle_timer);
        bsp_display_backlight(100);
    }
    if (was_off) {
        if (s_needs_render) render();
        if (s_state.page == PDKPASS_PAGE_RESULTS ||
            s_state.page == PDKPASS_PAGE_RACE_DETAIL)
            pdkpass_results_request_race(s_state.selected_race);
        return;
    }

    pdkpass_input_t input;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        input = PDKPASS_INPUT_BACK;
    } else if (s_state.page == PDKPASS_PAGE_HOME &&
               btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {
        input = PDKPASS_INPUT_UP_LONG;
    } else if (s_state.page == PDKPASS_PAGE_HOME &&
               btn == BSP_BTN_DOWN && ev == BSP_BTN_LONG) {
        input = PDKPASS_INPUT_DOWN_LONG;
    } else if (ev != BSP_BTN_CLICK) {
        return;
    } else if (btn == BSP_BTN_UP) {
        input = PDKPASS_INPUT_UP;
    } else if (btn == BSP_BTN_DOWN) {
        input = PDKPASS_INPUT_DOWN;
    } else {
        input = PDKPASS_INPUT_OK;
    }

    pdkpass_state_t previous = s_state;
    if (s_state.page == PDKPASS_PAGE_NETWORK_PROGRESS) {
        if (input == PDKPASS_INPUT_BACK ||
            (input == PDKPASS_INPUT_OK && !s_hotspot_active &&
             s_network_state != PDKPASS_NETWORK_CONNECTING)) {
            pdkpass_network_request(PDKPASS_NETWORK_CANCEL);
            s_state.page = PDKPASS_PAGE_NETWORK;
            render();
        }
        return;
    }
    if ((s_state.page == PDKPASS_PAGE_NETWORK && input == PDKPASS_INPUT_OK &&
         s_state.network_selection < 2U) ||
        (s_state.page == PDKPASS_PAGE_NETWORK_CONFIRM && input == PDKPASS_INPUT_OK)) {
        bool setup = s_state.network_selection == 1U;
        bool connected = s_network_state == PDKPASS_NETWORK_ONLINE ||
                         s_network_state == PDKPASS_NETWORK_SYNCING ||
                         s_network_state == PDKPASS_NETWORK_TIME_ERROR;
        if (!setup && connected) { s_state.page = PDKPASS_PAGE_HOME; render(); return; }
        if (setup && connected && s_state.page != PDKPASS_PAGE_NETWORK_CONFIRM) {
            s_state.page = PDKPASS_PAGE_NETWORK_CONFIRM;
            render(); return;
        }
        s_state.page = PDKPASS_PAGE_NETWORK_PROGRESS;
        s_network_state = PDKPASS_NETWORK_CONNECTING;
        s_setup_error[0] = '\0';
        render();
        pdkpass_network_request(setup ? PDKPASS_NETWORK_OPEN_SETUP : PDKPASS_NETWORK_RETRY);
        return;
    }
    pdkpass_state_handle(&s_state, input,
                         s_season.race_count, s_season.driver_count);
    if (memcmp(&previous, &s_state, sizeof(s_state)) == 0) return;
    if (previous.page == PDKPASS_PAGE_NETWORK &&
        s_state.page == PDKPASS_PAGE_NETWORK &&
        update_network_selection(previous.network_selection,
                                 s_state.network_selection)) return;
    render();
    if (s_state.page == PDKPASS_PAGE_RESULTS ||
        s_state.page == PDKPASS_PAGE_RACE_DETAIL) {
        pdkpass_results_request_race(s_state.selected_race);
    }
}
