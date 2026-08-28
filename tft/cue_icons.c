/*
 * cue_icons.c - see cue_icons.h.
 */
#include "cue_icons.h"
#include "st7735.h"
#include "cue_icon_bitmaps.h"

void cue_icon_thermometer_draw(int16_t x, int16_t y, uint16_t bg) {
    (void)bg;
    st7735_draw_bitmap(x, y, THERM_ICON_SIZE, THERM_ICON_SIZE, cue_icon_thermometer);
}

void cue_icon_clock_draw(int16_t x, int16_t y, uint16_t bg) {
    (void)bg;
    st7735_draw_bitmap(x, y, CLOCK_ICON_SIZE, CLOCK_ICON_SIZE, cue_icon_clock);
}
