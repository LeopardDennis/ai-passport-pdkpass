#include "pdkpass_ui.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "pdkpass_data.h"
#include "pdkpass_font.h"
#include "pdkpass_power.h"
#include "pdkpass_model.h"
#include "pdkpass_results.h"
#include "pdkpass_reminder.h"
#include "pdkpass_sound.h"
#include "esp_timer.h"
#include "pdkpass_schedule.h"
#include "pdkpass_season_core.h"
#include "pdkpass_season.h"
#include "pdkpass_sync_policy.h"
#include "pdkpass_theme.h"
#include "pdkpass_tracks.h"
#include "pdkpass_wifi_qr.h"
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
#define FOOTER_TEXT_PAD 4
#define INNER_W 210
#define INNER_H 177
#define CALENDAR_ROWS 5
#define STANDINGS_ROWS 5
#define IDLE_DIM_SECONDS 30
#define IDLE_OFF_SECONDS 90
#define CLOCK_FALLBACK_PERIOD_MS 86400000U
#define BEIJING_OFFSET_SECONDS 28800LL

static lv_obj_t *s_screen;
static lv_obj_t *s_reminder_layer;
static lv_obj_t *s_status;
static lv_obj_t *s_content;
static lv_obj_t *s_hint_box;
static lv_obj_t *s_hint;
static char s_home_session_text[22];
static lv_obj_t *s_battery;
static lv_obj_t *s_battery_fill;
static lv_obj_t *s_battery_tip;
static int s_battery_soc = -1;
static lv_obj_t *s_network;
static lv_obj_t *s_status_left;
static lv_obj_t *s_status_right;

static lv_timer_t *s_idle_timer;
static lv_timer_t *s_clock_timer;
static lv_timer_t *s_sync_timer;
static pdkpass_state_t s_state;
static pdkpass_season_snapshot_t s_season;
static pdkpass_team_snapshot_t s_team_standings;

static bool s_time_valid;
static bool s_time_estimated;
static uint32_t s_last_activity;
static bool s_needs_render;
static int s_list_page = -1;
static size_t s_list_start;
static size_t s_list_selected = (size_t)-1;
static lv_obj_t *s_list_rows[STANDINGS_ROWS];
static lv_obj_t *s_list_footer;
static lv_obj_t *s_network_cards[4];
static lv_timer_t *s_reminder_timer;
static bool s_reminder_visible, s_reminder_was_dark;
static uint32_t s_reminder_previous_activity;
static pdkpass_reminder_entry_t s_reminder_alert;
static unsigned s_idle_stage;
static uint32_t s_status_background = UI_SKY;
static uint32_t s_battery_background = UINT32_MAX;
static pdkpass_network_state_t s_network_state = PDKPASS_NETWORK_STARTING;
static char s_setup_ssid[33];
static char s_setup_password[16];
static int64_t s_sync_cooldown_until_us;
static char s_setup_error[32];
static unsigned s_setup_seconds_left;
static bool s_hotspot_active;
static bool s_setup_show_info;
static bool s_setup_qr_failed;
static lv_obj_t *s_setup_countdown;
static lv_point_precise_t s_track_points[49];
static uint32_t s_results_sync_generation, s_points_sync_generation;
static uint32_t s_results_notice_until, s_points_notice_until;
static uint32_t s_sync_reject_until;
static pdkpass_page_t s_sync_reject_page;
static char s_sync_reject_hint[28];
static void render(void);

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

static lv_obj_t *make_fit_body_label(lv_obj_t *parent, const char *text,
                                     int x, int y, int width, uint32_t color)
{
    lv_point_t size;
    lv_text_get_size(&size, text, &pdkpass_body_font, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int source_width = size.x + 2;
    lv_obj_t *label = make_label(parent, text, x, y, source_width,
                                 &pdkpass_body_font, color);
    if (source_width > width) {
        lv_obj_set_style_transform_pivot_x(label, 0, 0);
        lv_obj_set_style_transform_scale_x(label,
                                           width * 256 / source_width, 0);
    }
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
        lv_obj_set_style_transform_scale_x(label, scale, 0);
        // Long names need a narrow fit, but should keep the same visual
        // prominence as shorter races instead of shrinking in both axes.
        lv_obj_set_style_transform_scale_y(label,
            scale < 384 ? 384 : scale, 0);
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

// sRGB channel luminance scaled to 0..65535. Several bright circuit accents
// need dark lettering, despite their average RGB value looking mid-range.
static const uint16_t s_srgb_linear[256] = {
        0,    20,    40,    60,    80,    99,   119,   139,   159,   179,   199,   219,   241,   264,   288,   313,
      340,   367,   396,   427,   458,   491,   526,   562,   599,   637,   677,   718,   761,   805,   851,   898,
      947,   997,  1048,  1101,  1156,  1212,  1270,  1330,  1391,  1453,  1517,  1583,  1651,  1720,  1790,  1863,
     1937,  2013,  2090,  2170,  2250,  2333,  2418,  2504,  2592,  2681,  2773,  2866,  2961,  3058,  3157,  3258,
     3360,  3464,  3570,  3678,  3788,  3900,  4014,  4129,  4247,  4366,  4488,  4611,  4736,  4864,  4993,  5124,
     5257,  5392,  5530,  5669,  5810,  5953,  6099,  6246,  6395,  6547,  6700,  6856,  7014,  7174,  7335,  7500,
     7666,  7834,  8004,  8177,  8352,  8528,  8708,  8889,  9072,  9258,  9445,  9635,  9828, 10022, 10219, 10417,
    10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090, 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909,
    14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
    18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721,
    23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325, 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094,
    28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
    34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891,
    41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534, 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359,
    48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
    57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535,
};

static uint32_t color_luminance(uint32_t color)
{
    uint32_t r = s_srgb_linear[(color >> 16) & 0xffU];
    uint32_t g = s_srgb_linear[(color >> 8) & 0xffU];
    uint32_t b = s_srgb_linear[color & 0xffU];
    return (r * 2126U + g * 7152U + b * 722U + 5000U) / 10000U;
}

static bool has_text_contrast(uint32_t background, uint32_t foreground)
{
    uint32_t a = color_luminance(background);
    uint32_t b = color_luminance(foreground);
    uint32_t lighter = a > b ? a : b;
    uint32_t darker = a > b ? b : a;
    // (lighter + 0.05) / (darker + 0.05) >= 4.5
    return 2U * (lighter + 3277U) >= 9U * (darker + 3277U);
}

static uint32_t readable_text_color(uint32_t background)
{
    if (has_text_contrast(background, UI_PAPER)) return UI_PAPER;
    if (has_text_contrast(background, UI_INK)) return UI_INK;
    return color_luminance(background) < 11750U ? 0xFFFFFF : 0x000000;
}

static uint32_t color_mix(uint32_t color, uint32_t other, unsigned other_weight)
{
    unsigned keep = 100U - other_weight;
    unsigned r = (((color >> 16) & 0xffU) * keep +
                  ((other >> 16) & 0xffU) * other_weight + 50U) / 100U;
    unsigned g = (((color >> 8) & 0xffU) * keep +
                  ((other >> 8) & 0xffU) * other_weight + 50U) / 100U;
    unsigned b = ((color & 0xffU) * keep +
                  (other & 0xffU) * other_weight + 50U) / 100U;
    return (r << 16) | (g << 8) | b;
}

// Scale from the glyph center so all reminder text stays aligned on the
// 240-pixel display. Long circuit and session names fit without clipping.
static lv_obj_t *reminder_label(lv_obj_t *parent, const char *text,
                                 int center_y, int max_width,
                                 const lv_font_t *font, int scale,
                                 uint32_t color)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int width = size.x > 0 ? size.x + 2 : 2;
    int height = size.y > 0 ? size.y : 16;
    int scale_x = width * scale > max_width * 256 ?
                  max_width * 256 / width : scale;
    lv_obj_t *label = make_label(parent, text, (240 - width) / 2,
                                  center_y - height / 2, width, font, color);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_pivot_x(label, width / 2, 0);
    lv_obj_set_style_transform_pivot_y(label, height / 2, 0);
    lv_obj_set_style_transform_scale_x(label, scale_x, 0);
    lv_obj_set_style_transform_scale_y(label, scale, 0);
    return label;
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

static uint32_t result_team_text_color(const char *team, uint32_t background,
                                       uint32_t driver_ink)
{
    if (team && strcmp(team, "MERCEDES") == 0) return 0x002855;
    unsigned r = (background >> 16) & 0xff;
    unsigned g = (background >> 8) & 0xff;
    unsigned b = background & 0xff;
    // The card already uses the team's color. Keep its color family in the
    // team label, but shift brightness to read against the same background.
    if (r * 299 + g * 587 + b * 114 < 70000) {
        return (((r * 3 + 255 * 7) / 10) << 16) |
               (((g * 3 + 255 * 7) / 10) << 8) |
               ((b * 3 + 255 * 7) / 10);
    }
    unsigned retain = driver_ink == UI_INK ? 3 : 1;
    return ((r * retain / 10) << 16) |
           ((g * retain / 10) << 8) |
           (b * retain / 10);
}

static void podium_display_name(const pdkpass_podium_driver_t *result,
                                char *out, size_t capacity)
{
    const char *surname = result->name[0] ? result->name : result->code;
    for (size_t i = 0; i < s_season.driver_count; i++) {
        const pdkpass_driver_t *known = &s_season.drivers[i];
        if (strncmp(result->code, known->code, sizeof(result->code)) == 0 &&
            known->first_name[0] && strcmp(surname, known->name) == 0) {
            snprintf(out, capacity, "%s %s", known->first_name, surname);
            return;
        }
    }
    snprintf(out, capacity, "%s", surname);
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

static const char *circuit_display_name(const char *circuit)
{
    return strcmp(circuit, "SPA-FRANCORCHAMPS") == 0 ? "SPA" : circuit;
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

static uint32_t race_session_mask(size_t race_index)
{
    if (race_index >= s_season.race_count) return 0;
    const pdkpass_race_t *race = &s_season.races[race_index];
    uint32_t fallback = (1U << PDKPASS_SESSION_FP1) |
                        (1U << PDKPASS_SESSION_QUALIFYING) |
                        (1U << PDKPASS_SESSION_RACE);
    if (strncmp(race->session_two_cn, "SPR Q", 5U) == 0) {
        fallback |= (1U << PDKPASS_SESSION_SPRINT_QUALIFYING) |
                    (1U << PDKPASS_SESSION_SPRINT);
    } else if (strncmp(race->session_two_cn, "QUALI", 5U) == 0) {
        fallback |= (1U << PDKPASS_SESSION_FP2) | (1U << PDKPASS_SESSION_FP3);
    }
    uint32_t mask = 0;
    for (unsigned kind = 0; kind < PDKPASS_SESSION_COUNT; kind++) {
        pdkpass_result_snapshot_t result;
        bool known = pdkpass_results_get(race_index,
            (pdkpass_session_kind_t)kind, &result) &&
            result.status != PDKPASS_RESULT_UNKNOWN;
        // Cached/API metadata wins over the bundled weekend format. A session
        // need not have a downloaded podium to remain visible. Cancellation
        // remains visible as an explanation, unlike an unscheduled session.
        if (known ? result.status != PDKPASS_RESULT_NOT_HELD
                  : (fallback & (1U << kind)) != 0)
            mask |= 1U << kind;
    }
    return mask;
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
        lv_color_hex(readable_text_color(background)), 0);
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
    if (strcmp(lv_label_get_text(s_hint), text) == 0) return;
    lv_label_set_text(s_hint, text);

    // Fit long hints in width only; shorter status messages restore the
    // full available-width centered label and unscaled text height.
    lv_point_t size;
    lv_text_get_size(&size, text, &pdkpass_body_font, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int available_width = FOOTER_W - 6 - 2 * FOOTER_TEXT_PAD;
    const int source_width = size.x + 2;
    const int scale = ui_pixel_fit_scale(source_width, available_width, 256);
    const int label_width = scale < 256 ? source_width : available_width;
    lv_obj_set_width(s_hint, label_width);
    lv_obj_set_style_transform_pivot_x(s_hint, 0, 0);
    lv_obj_set_style_transform_scale_x(s_hint, scale, 0);
    lv_obj_set_x(s_hint,
        FOOTER_TEXT_PAD + (available_width - label_width * scale / 256) / 2);
}

static bool sync_notice_active(uint32_t until)
{
    return until && (int32_t)(until - lv_tick_get()) > 0;
}

static void update_manual_hint(void)
{
    if (s_sync_reject_page == s_state.page && s_sync_cooldown_until_us) {
        int64_t remaining = s_sync_cooldown_until_us - esp_timer_get_time();
        if (remaining > 0) {
            char cooldown[32];
            snprintf(cooldown, sizeof(cooldown), "WAIT %us TO SYNC", (unsigned)((remaining + 999999) / 1000000));
            set_hint(cooldown);
            return;
        }
        s_sync_cooldown_until_us = 0;
        s_sync_reject_until = 0;
    }
    bool results = s_state.page == PDKPASS_PAGE_RESULTS;
    bool points = s_state.page == PDKPASS_PAGE_STANDINGS ||
                  s_state.page == PDKPASS_PAGE_TEAM_STANDINGS;
    if (!results && !points) return;
    if (s_sync_reject_page == s_state.page &&
        sync_notice_active(s_sync_reject_until)) {
        set_hint(s_sync_reject_hint);
        return;
    }
    pdkpass_manual_status_t status;
    char hint[32];
    if (results) {
        size_t race_index;
        if (pdkpass_results_manual_status(&race_index, &status) &&
            race_index == s_state.selected_race &&
            status.session == (unsigned)s_state.selected_session &&
            (status.state == PDKPASS_MANUAL_RUNNING ||
             sync_notice_active(s_results_notice_until))) {
            const char *word = status.state == PDKPASS_MANUAL_RUNNING ? "SYNCING..." :
                status.state == PDKPASS_MANUAL_UPDATED ? "UPDATED" :
                status.state == PDKPASS_MANUAL_UNCHANGED ? "NO NEW DATA" :
                status.state == PDKPASS_MANUAL_PARTIAL ? "PARTIAL UPDATE" :
                status.state == PDKPASS_MANUAL_NOT_READY ? "RESULT PENDING" :
                status.state == PDKPASS_MANUAL_OFFLINE ? "WIFI OFFLINE" :
                status.state == PDKPASS_MANUAL_TIMED_OUT ? "SYNC TIMEOUT" : "SYNC FAILED";
            snprintf(hint, sizeof(hint), "%s %s",
                     detail_session_short_label(status.session), word);
            set_hint(hint);
            return;
        }
        set_hint("UP/DN OK:SYNC HOLD:BACK");
        return;
    }
    if (pdkpass_season_manual_status(&status) &&
        (status.state == PDKPASS_MANUAL_RUNNING ||
         sync_notice_active(s_points_notice_until))) {
        const char *word = status.state == PDKPASS_MANUAL_RUNNING ? "SYNCING..." :
            status.state == PDKPASS_MANUAL_UPDATED ? "UPDATED" :
            status.state == PDKPASS_MANUAL_UNCHANGED ? "NO NEW DATA" :
            status.state == PDKPASS_MANUAL_PARTIAL ? "PARTIAL UPDATE" :
            status.state == PDKPASS_MANUAL_OFFLINE ? "WIFI OFFLINE" :
            status.state == PDKPASS_MANUAL_TIMED_OUT ? "SYNC TIMEOUT" : "SYNC FAILED";
        snprintf(hint, sizeof(hint), "POINTS %s", word);
        set_hint(hint);
        return;
    }
    set_hint("UP/DN OK:SYNC HOLD:HOME");
}

static void manual_sync_tick(lv_timer_t *timer)
{
    if (s_idle_stage == 2) {
        if (timer) lv_timer_pause(timer);
        return;
    }
    pdkpass_manual_status_t status;
    size_t race_index;
    bool redraw_results = false;
    if (pdkpass_results_manual_status(&race_index, &status) &&
        status.generation != s_results_sync_generation) {
        s_results_sync_generation = status.generation;
        s_results_notice_until = status.state == PDKPASS_MANUAL_RUNNING
            ? 0U : lv_tick_get() + 3000U;
        redraw_results = s_state.page == PDKPASS_PAGE_RESULTS &&
                         race_index == s_state.selected_race &&
                         status.session == (unsigned)s_state.selected_session;
    }
    if (pdkpass_season_manual_status(&status) &&
        status.generation != s_points_sync_generation) {
        s_points_sync_generation = status.generation;
        s_points_notice_until = status.state == PDKPASS_MANUAL_RUNNING
            ? 0U : lv_tick_get() + 3000U;
    }
    if (redraw_results) render();
    update_manual_hint();
}

static void title_for_season(char *output, size_t capacity,
                             const char *prefix)
{
    snprintf(output, capacity, "%s %02u", prefix,
             (unsigned)(s_season.year % 100U));
}

#include "pdkpass_ui_network.inc"

static void render_season_complete(void)
{
    memset(&s_team_standings, 0, sizeof(s_team_standings));
    pdkpass_season_team_snapshot(&s_team_standings);
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
    set_hint("UP/DN:BROWSE RACES");
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
        make_card(s_content, x, 168, width, 9, color, 2);
        x += width + gap;
    }
}

// Offline calendars contain only three published session times. Do not infer
// missing practice/sprint times; complete discovered schedules take precedence.
static int64_t home_calendar_start(const char *line)
{
    char month[4], extra;
    unsigned day, hour, minute;
    const char *date = strchr(line, ' ');
    while (date) {
        while (*date == ' ') date++;
        if (*date >= '0' && *date <= '9') break;
        date = strchr(date, ' ');
    }
    if (!date || sscanf(date, "%u %3s %u:%u %c", &day, month,
                       &hour, &minute, &extra) != 4 || day < 1 || day > 31 ||
        hour > 23 || minute > 59) return 0;
    static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    unsigned m = 0;
    while (m < 12 && strcmp(month, months[m]) != 0) m++;
    if (m == 12) return 0;
    char iso[40];
    snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:00+08:00",
             s_season.year, m + 1, day, hour, minute);
    int64_t start = 0;
    return pdkpass_parse_iso8601_utc(iso, &start) ? start : 0;
}

static int64_t home_next_session(char *output, size_t capacity)
{
    if (!s_time_valid) {
        snprintf(output, capacity, "SYNC CLOCK");
        return 0;
    }
    const pdkpass_race_t *race = &s_season.races[s_state.selected_race];
    pdkpass_reminder_entry_t entries[PDKPASS_SESSION_COUNT] = {0};
    bool complete = pdkpass_reminder_round_schedule(s_season.year,
                                                   race->meeting_key, entries);
    if (!complete) {
        entries[PDKPASS_SESSION_FP1].start_utc = home_calendar_start(race->session_one_cn);
        entries[detail_middle_session(race)].start_utc = home_calendar_start(race->session_two_cn);
        entries[PDKPASS_SESSION_RACE].start_utc = home_calendar_start(race->race_cn);
        for (unsigned i = 0; i < PDKPASS_SESSION_COUNT; i++) {
            pdkpass_result_snapshot_t result;
            if (pdkpass_results_get(s_state.selected_race, i, &result) &&
                (result.status == PDKPASS_RESULT_CANCELLED ||
                 result.status == PDKPASS_RESULT_NOT_HELD ||
                 result.status == PDKPASS_RESULT_READY))
                entries[i].flags |= PDKPASS_REMINDER_CANCELLED;
        }
    }
    int64_t now = (int64_t)time(NULL);
    int next = pdkpass_reminder_next_session(entries, now);
    if (next < 0) {
        snprintf(output, capacity, "%s", complete || now >= race->switch_at_utc
                                         ? "Completed" : "SCHEDULE TBD");
        return 0;
    }
    time_t local = (time_t)(entries[next].start_utc + BEIJING_OFFSET_SECONDS);
    struct tm tm;
    gmtime_r(&local, &tm);
    snprintf(output, capacity, "%s %02d:%02d CST",
             detail_session_short_label(next), tm.tm_hour, tm.tm_min);
    return entries[next].start_utc;
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
    memset(&s_team_standings, 0, sizeof(s_team_standings));
    pdkpass_season_team_snapshot(&s_team_standings);
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
    const char *circuit_name = circuit_display_name(race->circuit);
    lv_point_t circuit_size;
    lv_text_get_size(&circuit_size, circuit_name, &lv_font_unscii_16,
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *circuit_font = circuit_size.x > INNER_W - 6
                                        ? &lv_font_unscii_8
                                        : &lv_font_unscii_16;
    make_center_label(s_content, circuit_name, 0, 73, INNER_W,
                      circuit_font, 0xFFFFFF);
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

    lv_obj_t *race_card = make_card(s_content, 8, 113, 194, 27,
                                    race->accent, 3);
    char race_time[22];
    int64_t next_start = home_next_session(race_time, sizeof(race_time));
    snprintf(s_home_session_text, sizeof(s_home_session_text), "%s", race_time);
    if (s_clock_timer && s_time_valid) {
        int64_t now = (int64_t)time(NULL);
        int64_t deadline = pdkpass_schedule_next_check(now, s_season.races,
                                                      s_season.race_count);
        if (next_start > now && next_start < deadline) deadline = next_start;
        uint64_t delay = deadline > now ? (uint64_t)(deadline - now) * 1000U : 1000U;
        lv_timer_set_period(s_clock_timer, delay > UINT32_MAX ? UINT32_MAX : (uint32_t)delay);
        lv_timer_reset(s_clock_timer);
    }
    make_medium_label(race_card, race_time, 0, -2, 188,
                      contrast_color(race->accent));
    char page[20];
    snprintf(page, sizeof(page), "%u / %u",
             (unsigned)(s_state.selected_race + 1U),
             (unsigned)s_season.race_count);
    make_center_label(s_content, page, 0, 146, INNER_W,
                      &lv_font_unscii_16, UI_PAPER);
    make_progress(s_state.selected_race, s_season.race_count);
    set_hint("UP/DN:RACE OK:VIEW");
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
            ? s_season.races[index].accent
            : page == PDKPASS_PAGE_TEAM_STANDINGS ? s_team_standings.teams[index].accent
                                                : s_season.drivers[index].accent;
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
    } else if (page == PDKPASS_PAGE_TEAM_STANDINGS && selected < count) {
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
    set_hint("UP/DN OK:VIEW HOLD:HOME");
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
        make_center_label(s_content, "CONNECT TO UPDATE", 0, 91,
                          INNER_W, &lv_font_unscii_8, UI_PAPER);
        make_center_label(s_content, "DRIVER DATA PENDING", 0, 115,
                          INNER_W, &lv_font_unscii_8, UI_PAPER);
        set_hint("UP/DN OK:SYNC HOLD:HOME");
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
    set_hint("UP/DN OK:SYNC HOLD:HOME");
}

static void render_team_standings(void)
{
    size_t start = (s_state.selected_team / STANDINGS_ROWS) *
                   STANDINGS_ROWS;
    if (update_list_selection(PDKPASS_PAGE_TEAM_STANDINGS, start, s_state.selected_team,
                              s_team_standings.count)) return;
    char title[20];
    title_for_season(title, sizeof(title), "TEAM POINTS");
    set_title(title);
    char status[32];
    snprintf(status, sizeof(status), "POINTS | %s",
             s_team_standings.as_of);
    set_status(status, UI_RED);
    content_reset(0x17202A, 0x263743);

    if (s_team_standings.count == 0U) {
        make_zoom_label(s_content, "POINTS", 0, 34, INNER_W, UI_YELLOW);
        make_center_label(s_content, "CONNECT TO UPDATE", 0, 91,
                          INNER_W, &lv_font_unscii_8, UI_PAPER);
        make_center_label(s_content, "TEAM DATA PENDING", 0, 115,
                          INNER_W, &lv_font_unscii_8, UI_PAPER);
        set_hint("UP/DN OK:SYNC HOLD:HOME");
        return;
    }

    for (size_t row = 0; row < STANDINGS_ROWS; row++) {
        size_t index = start + row;
        if (index >= s_team_standings.count) break;
        const pdkpass_team_t *driver = &s_team_standings.teams[index];
        bool selected = index == s_state.selected_team;
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
        make_fit_body_label(card, driver->name, 32, 6, 120, ink);
        lv_obj_t *score = make_label(card, points, 156, 8, 42,
                                     &lv_font_unscii_8, ink);
        lv_obj_set_style_text_align(score, LV_TEXT_ALIGN_RIGHT, 0);
    }
    char page[20];
    snprintf(page, sizeof(page), "%u / %u", (unsigned)(s_state.selected_team + 1U),
             (unsigned)s_team_standings.count);
    s_list_footer = make_center_label(s_content, page, 0, 165, INNER_W,
                                      &lv_font_unscii_8, UI_PAPER);
    s_list_page = PDKPASS_PAGE_TEAM_STANDINGS;
    s_list_start = start;
    s_list_selected = s_state.selected_team;
    set_hint("UP/DN OK:SYNC HOLD:HOME");
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
    ui_pixel_screen_set_round_title(s_screen,
                                    circuit_display_name(race->circuit),
                                    race->round);
    char status[32];
    snprintf(status, sizeof(status), "%s GP", race->country);
    set_status(status, race->accent);
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
    uint32_t sessions = race_session_mask(s_state.selected_race);
    unsigned visible_row = 0;
    for (size_t i = 0; i < 3U; i++) {
        if (!(sessions & (1U << session_kinds[i]))) continue;
        char line[PDKPASS_SESSION_LINE_LEN];
        detail_session_line(s_state.selected_race, session_kinds[i],
                            session_schedules[i], line, sizeof(line));
        uint32_t bg = i == 2U ? UI_YELLOW : UI_PAPER;
        lv_obj_t *row = make_card(s_content, 1, 109 + (int)visible_row++ * 22,
                                  208, 21, bg, 2);
        make_center_label(row, line, 1, 4, 204,
                          &lv_font_unscii_8, UI_INK);
    }
    set_hint("UP/DN OK:VIEW HOLD:BACK");
}

static void render_results(void)
{
    if (s_state.selected_race >= s_season.race_count) return;
    const pdkpass_race_t *race = &s_season.races[s_state.selected_race];
    ui_pixel_screen_set_round_title(s_screen, race->country, race->round);
    pdkpass_state_set_sessions(&s_state, race_session_mask(s_state.selected_race));
    content_reset(0x17202A, 0x263743);
    if (s_state.selected_session >= PDKPASS_SESSION_COUNT) {
        set_status("SESSIONS", race->accent);
        make_medium_label(s_content, "NO SESSIONS", 0, 42, INNER_W, UI_YELLOW);
        make_medium_label(s_content, "CHECK SCHEDULE", 0, 95, INNER_W, UI_PAPER);
        set_hint("UP/DN OK:SYNC HOLD:BACK");
        return;
    }
    set_status(pdkpass_session_label(s_state.selected_session), race->accent);

    pdkpass_result_snapshot_t result;
    bool available = pdkpass_results_get(s_state.selected_race,
                                         s_state.selected_session, &result);
    if (!available || result.status != PDKPASS_RESULT_READY) {
        bool online = s_network_state == PDKPASS_NETWORK_ONLINE;
        const char *state = online ? "SYNC PENDING" : "CONNECT TO UPDATE";
        const char *detail = "NO RESULT CACHED";
        const char *note = online ? "AUTO RETRY ENABLED" : "NETWORK: RETRY";
        if (available && result.status == PDKPASS_RESULT_NOT_HELD) {
            state = "NO SESSION";
            detail = "NOT ON THIS WEEKEND";
            note = "";
        } else if (available && result.status == PDKPASS_RESULT_CANCELLED) {
            state = "SESSION CANCELLED";
            detail = "NO CLASSIFICATION";
            note = "";
        } else if (!s_time_valid) {
            state = "SYNC CLOCK";
            detail = "CONNECT FOR TIME";
            note = "NETWORK: RETRY";
        } else if (available && result.status == PDKPASS_RESULT_SCHEDULED &&
                   result.session_end_utc > 0) {
            if (!pdkpass_session_result_due((int64_t)time(NULL),
                                            result.session_end_utc)) {
                state = "RESULT PENDING";
                detail = "SYNC AFTER SESSION";
                note = "SYNC ABOUT +30 MIN";
            } else {
                // Metadata is known, but a completed session's missing podium
                // is a download/retry state, not a future result.
                detail = "RESULT NOT DOWNLOADED";
            }
        } else {
            // Persisted session metadata omits end times until rediscovery.
            detail = "CHECKING SESSION TIME";
        }
        make_medium_label(s_content, state, 0, 42, INNER_W, UI_YELLOW);
        make_medium_label(s_content, detail, 0, 95, INNER_W, UI_PAPER);
        make_center_label(s_content, note, 0, 139,
                          INNER_W, &lv_font_unscii_8, UI_PAPER);
    } else {
        for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
            const pdkpass_podium_driver_t *driver = &result.podium[i];
            uint32_t background = result_driver_accent(driver, race->accent);
            uint32_t ink = contrast_color(background);
            uint32_t team_ink = result_team_text_color(driver->team,
                                                       background, ink);
            lv_obj_t *row = make_card(s_content, 2, 7 + (int)i * 55,
                                      206, 49, background, 3);
            char position[4];
            char display_name[32];
            snprintf(position, sizeof(position), "P%u", driver->position);
            podium_display_name(driver, display_name, sizeof(display_name));
            make_label(row, position, 7, 5, 32, &lv_font_unscii_16, ink);
            make_label(row, driver->code, 7, 27, 32,
                       &lv_font_unscii_8, ink);
            make_fit_body_label(row, display_name, 45, 5, 153, ink);
            make_label(row, driver->team, 45, 27, 153,
                       &lv_font_unscii_8, team_ink);
        }
    }
    set_hint("UP/DN OK:SYNC HOLD:BACK");
}

static void render_reminder(void)
{
    const pdkpass_race_t *race = NULL;
    for (size_t i = 0; i < s_season.race_count; i++) {
        if (s_season.races[i].round == s_reminder_alert.round) {
            race = &s_season.races[i];
            break;
        }
    }
    pdkpass_theme_t theme = theme_for_race(race);
    const pdkpass_track_info_t *track = race ? pdkpass_track_find(race->circuit) : NULL;
    uint32_t background = track ? track->background : theme.top;
    uint32_t accent = track ? track->accent :
                      (race ? race->accent : UI_YELLOW);
    bool same_color = accent == background;
    uint32_t card_color = same_color ? color_mix(accent, UI_PAPER, 55) : accent;
    uint32_t panel_color = same_color ? color_mix(background, UI_INK, 38) :
                           theme.bottom;

    // The normal page is rebuilt when the alert closes. Free its content
    // before creating the full-screen reminder so the LVGL pool stays small.
    content_reset(theme.top, theme.bottom);
    if (s_reminder_layer) lv_obj_delete(s_reminder_layer);
    s_reminder_layer = make_block(s_screen, 0, 0, 240, 320, background);
    set_gradient(s_reminder_layer, background, theme.bottom);

    make_block(s_reminder_layer, 4, 4, 232, 48, UI_INK);
    make_block(s_reminder_layer, 6, 7, 228, 44, background);
    make_block(s_reminder_layer, 10, 11, 220, 36, UI_RED);
    reminder_label(s_reminder_layer, "RACE ALERT", 29, 204,
                   &lv_font_unscii_16, 384, UI_PAPER);

    const char *circuit = race ? circuit_display_name(race->circuit) : "GRAND PRIX";
    reminder_label(s_reminder_layer, circuit, 75, 206,
                   &lv_font_unscii_16, 448, readable_text_color(background));
    char line[40];
    snprintf(line, sizeof(line), "ROUND %u", s_reminder_alert.round);
    reminder_label(s_reminder_layer, line, 101, 204,
                   &pdkpass_body_font, 256, readable_text_color(background));

    make_block(s_reminder_layer, 6, 115, 228, 114, UI_INK);
    make_block(s_reminder_layer, 10, 119, 216, 106, card_color);
    uint32_t card_ink = readable_text_color(card_color);
    reminder_label(s_reminder_layer, "STARTS IN", 134, 188,
                   &lv_font_unscii_16, 256, card_ink);
    make_block(s_reminder_layer, 31, 145, 174, 2, card_ink);
    int64_t remaining = s_reminder_alert.start_utc - (int64_t)time(NULL);
    if (remaining < 0) remaining = 0;
    snprintf(line, sizeof(line), "%ld", (long)((remaining + 59) / 60));
    reminder_label(s_reminder_layer, line, 177, 190,
                   &lv_font_unscii_16, 896, card_ink);
    reminder_label(s_reminder_layer, "MINUTES", 211, 188,
                   &lv_font_unscii_16, 256, card_ink);

    make_block(s_reminder_layer, 6, 232, 228, 52, UI_INK);
    make_block(s_reminder_layer, 10, 236, 220, 44, panel_color);
    uint32_t panel_ink = readable_text_color(panel_color);
    const char *names[] = {"PRACTICE 1", "PRACTICE 2", "PRACTICE 3",
        "SPRINT QUALIFYING", "SPRINT", "QUALIFYING", "RACE"};
    if (s_reminder_alert.kind < PDKPASS_SESSION_COUNT)
        reminder_label(s_reminder_layer, names[s_reminder_alert.kind], 250, 204,
                       &lv_font_unscii_16, 352, panel_ink);
    pdkpass_format_beijing_session("START", s_reminder_alert.start_utc, line, sizeof(line));
    char date_line[40];
    snprintf(date_line, sizeof(date_line), "%s CST", line + 6);
    reminder_label(s_reminder_layer, date_line, 270, 204,
                   &pdkpass_body_font, 256, panel_ink);

    make_block(s_reminder_layer, 4, 287, 232, 28, UI_INK);
    make_block(s_reminder_layer, 10, 291, 220, 20, UI_PAPER);
    reminder_label(s_reminder_layer, "ANY KEY TO DISMISS", 301, 206,
                   &pdkpass_body_font, 256, UI_INK);
}

static void render(void)
{
    if (s_idle_stage == 2) { s_needs_render = true; return; }
    s_needs_render = false;
    if (s_reminder_visible) { render_reminder(); return; }
    if (s_reminder_layer) {
        lv_obj_delete(s_reminder_layer);
        s_reminder_layer = NULL;
    }
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
    case PDKPASS_PAGE_TEAM_STANDINGS:
        render_team_standings();
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
    update_manual_hint();
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
    uint32_t ink = soc <= 20 ? UI_RED : readable_text_color(s_status_background);
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
    uint32_t ink = readable_text_color(s_status_background);
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
        else {
            char line[22];
            line[0] = '\0';
            if (s_state.selected_race < s_season.race_count) home_next_session(line, sizeof(line));
            if (!s_state.season_complete && strcmp(line, s_home_session_text) != 0) render();
            else update_home_status();
        }
    }

    int64_t deadline = pdkpass_schedule_next_check(now, s_season.races,
                                                   s_season.race_count);
    if (s_state.page == PDKPASS_PAGE_HOME &&
        s_state.selected_race < s_season.race_count && !s_state.season_complete) {
        char line[22];
        int64_t session_start = home_next_session(line, sizeof(line));
        if (session_start > now && session_start < deadline) deadline = session_start;
    }
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
    if (s_reminder_visible) return;
    uint32_t elapsed = lv_tick_get() - s_last_activity;
    if (elapsed >= IDLE_OFF_SECONDS * 1000U) {
        bsp_lvgl_set_drawing(false);
        bsp_display_sleep(true);
        pdkpass_power_display(false);
        s_idle_stage = 2;
        lv_timer_pause(s_sync_timer);
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

void pdkpass_ui_reminder_dismiss(void)
{
    if (!s_reminder_visible) return;
    s_reminder_visible = false;
    if (s_reminder_layer) {
        lv_obj_delete(s_reminder_layer);
        s_reminder_layer = NULL;
    }
    pdkpass_sound_reminder_stop();
    lv_timer_pause(s_reminder_timer);
    s_last_activity = s_reminder_previous_activity;
    if (s_reminder_was_dark) {
        bsp_lvgl_set_drawing(false);
        bsp_display_sleep(true);
        pdkpass_power_display(false);
        s_idle_stage = 2;
        lv_timer_pause(s_sync_timer);
        s_needs_render = true;
        lv_timer_pause(s_idle_timer);
    } else {
        bsp_display_backlight(100);
        render();
        idle_tick(s_idle_timer);
        if (s_idle_stage != 2) lv_timer_resume(s_idle_timer);
    }
}

static void reminder_timeout(lv_timer_t *timer)
{
    (void)timer;
    pdkpass_ui_reminder_dismiss();
}

void pdkpass_ui_reminder_show(const pdkpass_reminder_entry_t *alert)
{
    if (!s_screen || !alert) return;
    if (!s_reminder_visible) {
        s_reminder_was_dark = s_idle_stage == 2;
        s_reminder_previous_activity = s_last_activity;
    }
    s_reminder_alert = *alert;
    s_reminder_visible = true;
    pdkpass_power_display(true);
    if (s_idle_stage != 2 || bsp_display_sleep(false) == ESP_OK) {
        bsp_lvgl_set_drawing(true);
        s_idle_stage = 0;
        manual_sync_tick(NULL);
        lv_timer_reset(s_sync_timer);
        lv_timer_resume(s_sync_timer);
        bsp_display_backlight(100);
    } else {
        pdkpass_power_display(false);
    }
    lv_timer_pause(s_idle_timer);
    lv_timer_reset(s_reminder_timer);
    lv_timer_resume(s_reminder_timer);
    render();
    pdkpass_sound_reminder_play();
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

    memset(&s_team_standings, 0, sizeof(s_team_standings));
    pdkpass_season_team_snapshot(&s_team_standings);
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
    // Reserve equal text margins inside the ticket border.
    s_hint = make_label(s_hint_box, "", FOOTER_TEXT_PAD, 9,
                        FOOTER_W - 6 - 2 * FOOTER_TEXT_PAD,
                        &lv_font_unscii_8, UI_INK);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    render();
    pdkpass_ui_battery_update(-1);
    s_idle_timer = lv_timer_create(idle_tick, IDLE_DIM_SECONDS * 1000U, NULL);
    s_reminder_timer = lv_timer_create(reminder_timeout, 15000U, NULL);
    lv_timer_pause(s_reminder_timer);
    s_clock_timer = lv_timer_create(clock_tick, CLOCK_FALLBACK_PERIOD_MS, NULL);
    s_sync_timer = lv_timer_create(manual_sync_tick, 250U, NULL);
    lv_screen_load(s_screen);
}

static void show_manual_sync_rejection(pdkpass_manual_state_t state)
{
    s_sync_cooldown_until_us = 0;
    if (state == PDKPASS_MANUAL_COOLDOWN) {
        pdkpass_manual_status_t status;
        size_t race;
        bool ok = s_state.page == PDKPASS_PAGE_RESULTS
            ? pdkpass_results_manual_status(&race, &status) : pdkpass_season_manual_status(&status);
        if (ok) s_sync_cooldown_until_us = status.cooldown_until_us;
    }
    const char *text = state == PDKPASS_MANUAL_OFFLINE ? "WIFI OFFLINE" :
        state == PDKPASS_MANUAL_BUSY ? "SYNC IN PROGRESS" :
        state == PDKPASS_MANUAL_COOLDOWN ? "PLEASE WAIT" :
        state == PDKPASS_MANUAL_NOT_READY ? "RESULT PENDING" : "SYNC FAILED";
    if (s_state.page == PDKPASS_PAGE_RESULTS &&
        s_state.selected_race < s_season.race_count) {
        snprintf(s_sync_reject_hint, sizeof(s_sync_reject_hint), "R%u %s",
                 s_season.races[s_state.selected_race].round, text);
    } else {
        snprintf(s_sync_reject_hint, sizeof(s_sync_reject_hint), "POINTS %s", text);
    }
    s_sync_reject_page = s_state.page;
    s_sync_reject_until = lv_tick_get() + 3000U;
    update_manual_hint();
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
    bool credentials_changed =
        strcmp(s_setup_ssid, update->setup_ssid ? update->setup_ssid : "") != 0 ||
        strcmp(s_setup_password, update->setup_password ? update->setup_password : "") != 0;
    snprintf(s_setup_error, sizeof(s_setup_error), "%s",
             update->setup_error ? update->setup_error : "");
    snprintf(s_setup_ssid, sizeof(s_setup_ssid), "%s",
             update->setup_ssid ? update->setup_ssid : "");
    snprintf(s_setup_password, sizeof(s_setup_password), "%s",
             update->setup_password ? update->setup_password : "");
    if (!s_hotspot_active || (s_hotspot_active && !was_hotspot)) {
        s_setup_show_info = false;
        s_setup_qr_failed = false;
    } else if (credentials_changed) {
        s_setup_qr_failed = false;
    }
    if (s_hotspot_active && s_setup_error[0]) s_setup_show_info = true;
    if (s_state.page == PDKPASS_PAGE_NETWORK_PROGRESS &&
        (update->state == PDKPASS_NETWORK_ONLINE || update->state == PDKPASS_NETWORK_SYNCING ||
         (was_hotspot && !s_hotspot_active))) s_state.page = PDKPASS_PAGE_HOME;
    if (s_setup_countdown && !error_changed)
        lv_label_set_text_fmt(s_setup_countdown, "AUTO OFF %02u:%02u",
                             s_setup_seconds_left / 60U, s_setup_seconds_left % 60U);
    update_network_label();
    clock_tick(NULL);
    if (previous_state != s_network_state || error_changed ||
        was_hotspot != s_hotspot_active || credentials_changed) render();
}

void pdkpass_ui_results_update(size_t race_index)
{
    if ((s_state.page == PDKPASS_PAGE_HOME ||
         s_state.page == PDKPASS_PAGE_RESULTS ||
         s_state.page == PDKPASS_PAGE_RACE_DETAIL) &&
        s_state.selected_race == race_index) render();
}

void pdkpass_ui_sync_status_update(void)
{
    if (s_state.page == PDKPASS_PAGE_NETWORK) render();
}

bool pdkpass_ui_season_update(void)
{
    // Already serialized by LVGL: copy directly into persistent UI storage.
    if (!pdkpass_season_snapshot(&s_season)) return false;
    if (!pdkpass_season_team_snapshot(&s_team_standings)) return false;
    if (s_state.selected_team >= s_team_standings.count)
        s_state.selected_team = s_team_standings.count ? s_team_standings.count - 1U : 0U;
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
    return true;
}

void pdkpass_ui_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_reminder_visible) { pdkpass_ui_reminder_dismiss(); return; }
    bool was_off = s_idle_stage == 2;
    if (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG) {
        pdkpass_power_display(true);
        if (was_off) {
            if (bsp_display_sleep(false) != ESP_OK) {
                pdkpass_power_display(false);
                return; // Keep dark and retry on the next key.
            }
            bsp_lvgl_set_drawing(true);
        }
        s_last_activity = lv_tick_get();
        s_idle_stage = 0;
        if (was_off) {
            manual_sync_tick(NULL);
            lv_timer_reset(s_sync_timer);
            lv_timer_resume(s_sync_timer);
        }
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
    if (s_hotspot_active &&
        (s_state.page == PDKPASS_PAGE_HOME ||
         s_state.page == PDKPASS_PAGE_NETWORK_PROGRESS)) {
        if (input == PDKPASS_INPUT_OK && !s_setup_qr_failed) {
            s_setup_show_info = !s_setup_show_info;
            render();
        } else if (input == PDKPASS_INPUT_BACK) {
            pdkpass_network_request(PDKPASS_NETWORK_CANCEL);
            s_setup_show_info = false;
            s_state.page = PDKPASS_PAGE_NETWORK;
            render();
        }
        return;
    }
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
    if (s_state.page == PDKPASS_PAGE_NETWORK && input == PDKPASS_INPUT_OK &&
        s_state.network_selection == 2U) {
        pdkpass_reminder_set_enabled(!pdkpass_reminder_enabled());
        render();
        return;
    }
    if (input == PDKPASS_INPUT_OK &&
        (s_state.page == PDKPASS_PAGE_RESULTS ||
         s_state.page == PDKPASS_PAGE_STANDINGS ||
         s_state.page == PDKPASS_PAGE_TEAM_STANDINGS)) {
        pdkpass_manual_state_t state = s_state.page == PDKPASS_PAGE_RESULTS
            ? pdkpass_results_force_session(s_state.selected_race, s_state.selected_session)
            : pdkpass_season_force_points();
        if (state == PDKPASS_MANUAL_RUNNING) {
            s_sync_reject_until = 0U;
            s_sync_cooldown_until_us = 0;
            update_manual_hint();
        } else show_manual_sync_rejection(state);
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
        s_setup_show_info = false;
        s_setup_qr_failed = false;
        render();
        if (pdkpass_network_request(setup ? PDKPASS_NETWORK_OPEN_SETUP : PDKPASS_NETWORK_RETRY) != ESP_OK) {
            s_network_state = PDKPASS_NETWORK_OFFLINE;
            snprintf(s_setup_error, sizeof(s_setup_error), "START FAILED / RETRY");
            render();
        }
        return;
    }
    if (s_state.page == PDKPASS_PAGE_RESULTS)
        pdkpass_state_set_sessions(&s_state, race_session_mask(s_state.selected_race));
    pdkpass_state_handle(&s_state, input,
                         s_season.race_count, s_season.driver_count, s_team_standings.count);
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
