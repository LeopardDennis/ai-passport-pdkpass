// Private implementation: included by the owning service, with its existing lock and worker.
static void render_wifi_setup_info(void)
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
    s_setup_countdown = make_center_label(s_content, remaining, 0, 164, INNER_W,
                      &lv_font_unscii_8, UI_RED);
    set_hint(s_setup_qr_failed ? "HOLD:BACK" : "OK:SCAN  HOLD:BACK");
}

static bool render_wifi_setup_scan(void)
{
    char payload[128];
    if (!pdkpass_wifi_qr_payload(payload, sizeof(payload),
                                  s_setup_ssid, s_setup_password)) return false;

    set_title("PDKPASS WIFI");
    set_status(s_network_state == PDKPASS_NETWORK_CONNECTING ? "CONNECTING" : "SETUP MODE", UI_RED);
    content_reset(UI_PAPER, 0xE6E7DA);
    make_center_label(s_content, "SCAN TO CONNECT", 0, 4, INNER_W,
                      &lv_font_unscii_8, UI_SKY_DARK);

    lv_obj_t *frame = make_card(s_content, 37, 20, 136, 136, 0xFFFFFF, 2);
    lv_obj_t *qr = lv_qrcode_create(frame);
    if (!qr) return false;
    lv_qrcode_set_size(qr, 132);
    lv_qrcode_set_dark_color(qr, lv_color_hex(UI_INK));
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);
    lv_obj_set_pos(qr, 0, 0);
    if (lv_qrcode_update(qr, payload, strlen(payload)) != LV_RESULT_OK) return false;

    char remaining[28];
    snprintf(remaining, sizeof(remaining), "AUTO OFF %02u:%02u",
             s_setup_seconds_left / 60U, s_setup_seconds_left % 60U);
    s_setup_countdown = make_center_label(s_content, remaining, 0, 164, INNER_W,
                      &lv_font_unscii_8, UI_RED);
    set_hint("OK:INFO  HOLD:BACK");
    return true;
}

static void render_wifi_setup(void)
{
    if (!s_setup_show_info && !s_setup_qr_failed && render_wifi_setup_scan()) return;
    s_setup_qr_failed = !s_setup_show_info;
    render_wifi_setup_info();
}

static void format_sync_line(pdkpass_sync_status_t service, unsigned season_year,
                             int64_t last, bool cached, char *line, size_t size)
{
    static const char *const labels[] = {"CAL SYNC", "RESULTS", "DRIVERS", "TEAMS"};
    static const char *const cache_labels[] = {"CAL", "RESULT", "DRIVERS", "TEAMS"};
    if ((unsigned)service >= PDKPASS_SYNC_STATUS_COUNT) { if (size) line[0] = '\0'; return; }
    const char *label = labels[service];
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
                 cache_labels[service]);
    } else if (other_season) {
        snprintf(line, size, "%s %u NO SYNC",
                 cache_labels[service],
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
    for (unsigned i = 0; i < PDKPASS_SYNC_STATUS_COUNT; i++) {
        pdkpass_sync_status_t service = (pdkpass_sync_status_t)i;
        bool cached = service == PDKPASS_SYNC_STATUS_CALENDAR ? pdkpass_season_has_cached_data() :
                      service == PDKPASS_SYNC_STATUS_RESULTS ? pdkpass_results_has_cached_data() :
                      service == PDKPASS_SYNC_STATUS_DRIVERS ? s_season.driver_count > 0 :
                      s_team_standings.count > 0;
        char line[32];
        format_sync_line(service, s_season.year,
                         pdkpass_sync_last_success(service), cached,
                         line, sizeof(line));
        make_center_label(s_content, line, 0, 2 + (int)i * 14, INNER_W,
                          &lv_font_unscii_8, UI_PAPER);
    }
    const char *titles[] = {"WI-FI SETUP", "RETRY WI-FI",
        pdkpass_reminder_enabled() ? "ALERTS: ON" : "ALERTS: OFF", "BACK"};
    for (unsigned i = 0; i < 4U; i++) {
        bool selected = s_state.network_selection == i;
        uint32_t ink = selected ? UI_INK : UI_SKY_DARK;
        lv_obj_t *card = make_card(s_content, 5, 59 + (int)i * 30, 200, 28,
                                   selected ? UI_YELLOW : UI_PAPER, 2);
        s_network_cards[i] = card;
        // One centered action per card leaves vertical padding and a clear
        // gap between four rows in the fixed-height menu.
        make_center_label(card, titles[i], 2, 5, 192, &lv_font_unscii_16, ink);
    }
    set_hint("UP/DN OK:GO HOLD:BACK");
}

static bool update_network_selection(unsigned previous, unsigned selected)
{
    if (previous >= 4U || selected >= 4U ||
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
    set_hint(busy ? "HOLD:CANCEL" : "OK:MENU HOLD:MENU");
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
