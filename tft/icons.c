/*
 * icons.c - Status row icons, rendered from pre-baked bitmaps.
 *
 * Redesigned per feedback: mode icons must be unambiguous at a glance
 * (gear vs. hand, not two similar blobs), the sensor icon should stay
 * quiet normally and light up (blinking, in code - see display.c) on a
 * problem, the warning icon is a standard hazard triangle, and the alarm
 * icon needed to actually read as a bell (previous version was a
 * shapeless blob) - see input/art/render_icons.py for the render
 * pipeline. The heater icon (flame) was dropped from this column
 * entirely - heater state is now the horizontal bar + rays above the
 * face instead (heat_indicator.c).
 *
 * A failed sensor used to be shown as the bright bitmap plus a red
 * diagonal slash drawn on top - that slash made the thermometer shape
 * unrecognizable, so it's gone. The "on" bitmap itself (red + a small
 * "?" triangle badge) now carries the whole message, blinking via plain
 * ICON_ON/ICON_OFF like the warning/alarm icons.
 */
#include "icons.h"
#include "st7735.h"
#include "icon_bitmaps.h"

static void blit_icon(int16_t x, int16_t y, icon_state_t s,
                       const uint16_t *on, const uint16_t *off) {
    st7735_draw_bitmap(x, y, ICON_W, ICON_H, (s == ICON_OFF) ? off : on);
}

void icon_mode_auto(int16_t x, int16_t y, icon_state_t s, uint16_t bg) {
    (void)bg;
    blit_icon(x, y, s, icon_gear_on, icon_gear_off);
}

void icon_mode_manual(int16_t x, int16_t y, icon_state_t s, uint16_t bg) {
    (void)bg;
    blit_icon(x, y, s, icon_hand_on, icon_hand_off);
}

void icon_sensor(int16_t x, int16_t y, icon_state_t s, uint16_t bg) {
    (void)bg;
    blit_icon(x, y, s, icon_thermometer_on, icon_thermometer_off);
}

void icon_warning(int16_t x, int16_t y, icon_state_t s, uint16_t bg) {
    (void)bg;
    blit_icon(x, y, s, icon_warning_on, icon_warning_off);
}

void icon_alarm(int16_t x, int16_t y, icon_state_t s, uint16_t bg) {
    (void)bg;
    blit_icon(x, y, s, icon_bell_on, icon_bell_off);
}
