/*
 * text_console.c - see text_console.h.
 */
#include "text_console.h"
#include "st7735.h"
#include "font_bitmaps.h"

#include <stddef.h>

_Static_assert(FONT_W * TEXT_COLS <= ST7735_WIDTH, "text grid too wide for screen");
_Static_assert(FONT_H * TEXT_ROWS <= ST7735_HEIGHT, "text grid too tall for screen");

void text_console_clear(uint16_t bg) {
    st7735_fill_screen(bg);
}

void text_console_write_row(uint8_t row, const char *line, uint16_t bg) {
    (void)bg; /* glyph bitmaps are full-bleed (black bg baked in) */
    if (row >= TEXT_ROWS) return;

    int16_t y = row * FONT_H;
    int ended = 0; /* once the NUL is seen, pad the rest of the row with spaces */

    for (int col = 0; col < TEXT_COLS; col++) {
        char c = line ? line[col] : '\0';
        if (c == '\0') ended = 1;
        if (ended) c = ' ';

        uint8_t idx = (uint8_t)c;
        const uint16_t *glyph;
        if (idx >= FONT_FIRST_CHAR && idx < FONT_FIRST_CHAR + FONT_NUM_CHARS) {
            glyph = font_bitmaps[idx - FONT_FIRST_CHAR];
        } else {
            /* unsupported byte (control char, non-ASCII) - '?' rather
             * than indexing out of bounds into flash */
            glyph = font_bitmaps['?' - FONT_FIRST_CHAR];
        }
        st7735_draw_bitmap(col * FONT_W, y, FONT_W, FONT_H, glyph);
    }
}
