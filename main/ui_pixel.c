#include "ui_pixel.h"

#include <string.h>

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h,
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

lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

lv_obj_t *ui_pixel_ticket_create(lv_obj_t *parent, int x, int y, int w, int h,
                                 uint32_t color, bool shadow)
{
    if (shadow) block(parent, x + 4, y + 4, w, h, UI_INK);
    lv_obj_t *ticket = block(parent, x, y, w, h, color);
    lv_obj_set_style_border_color(ticket, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(ticket, 3, 0);
    return ticket;
}

lv_obj_t *ui_pixel_screen_create(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_SKY_DARK), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // A few offset rails give the printed-pass texture without filling the
    // small LVGL object pool with decorative elements.
    block(scr, 0, 0, 240, 4, UI_INK);
    block(scr, 0, 316, 240, 4, UI_INK);
    block(scr, 0, 46, 5, 224, UI_SKY);
    block(scr, 235, 46, 5, 224, 0x0B79CF);

    lv_obj_t *plate = ui_pixel_ticket_create(scr, 7, 6, 226, 38,
                                              UI_PAPER, true);
    lv_obj_t *heading = ui_pixel_label(plate, title, &lv_font_unscii_16,
                                       UI_INK);
    lv_obj_center(heading);
    lv_obj_set_user_data(scr, heading);
    return scr;
}

void ui_pixel_screen_set_title(lv_obj_t *screen, const char *title)
{
    if (!screen || !title) return;
    lv_obj_t *heading = lv_obj_get_user_data(screen);
    if (heading) {
        lv_label_set_text(heading, title);
        lv_obj_set_style_text_font(heading,
            strlen(title) > 13U ? &lv_font_unscii_8 : &lv_font_unscii_16, 0);
        lv_obj_center(heading);
    }
}

void ui_pixel_screen_set_theme(lv_obj_t *screen, uint32_t color,
                               uint32_t dark_color)
{
    if (!screen) return;
    // ui_pixel_screen_create owns these direct children: top rail, bottom rail,
    // left theme rail, right theme rail, title shadow, then title plate.
    lv_obj_t *left_rail = lv_obj_get_child(screen, 2);
    lv_obj_t *right_rail = lv_obj_get_child(screen, 3);
    if ((lv_color_to_u32(lv_obj_get_style_bg_color(screen, 0)) & 0xFFFFFFU) != dark_color)
        lv_obj_set_style_bg_color(screen, lv_color_hex(dark_color), 0);
    if (left_rail) {
        if ((lv_color_to_u32(lv_obj_get_style_bg_color(left_rail, 0)) & 0xFFFFFFU) != color)
            lv_obj_set_style_bg_color(left_rail, lv_color_hex(color), 0);
    }
    if (right_rail) {
        if ((lv_color_to_u32(lv_obj_get_style_bg_color(right_rail, 0)) & 0xFFFFFFU) != dark_color)
            lv_obj_set_style_bg_color(right_rail, lv_color_hex(dark_color), 0);
    }
}

lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color)
{
    lv_obj_t *panel = ui_pixel_ticket_create(parent, x, y, w, h, color, true);
    lv_obj_set_style_pad_all(panel, 4, 0);
    return panel;
}

lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y)
{
    (void)x;
    (void)y;
    // Compatibility shim for demos that still call the old helper. PDKPASS
    // deliberately has no mascot because the approved design is full-screen.
    lv_obj_t *placeholder = block(parent, 0, 0, 1, 1, UI_SKY_DARK);
    lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
    return placeholder;
}

void ui_pixel_mascot_jump(lv_obj_t *mascot)
{
    (void)mascot;
}

void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled)
{
    uint32_t color = !enabled ? 0x78909C : (selected ? UI_YELLOW : UI_PAPER);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(panel,
        lv_color_hex(selected ? 0xFFFFFF : UI_INK), 0);
}
