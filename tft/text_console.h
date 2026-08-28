/*
 * text_console.h - Full-screen fixed-font ASCII text console.
 *
 * A second display mode alongside the graphical warmer UI: a plain
 * 21x16 character grid (6x10 baked font, see art/render_font.py),
 * driven by row-addressed commands (clear / seek / write) rather than
 * a persistent framebuffer - display.c owns the cursor row and
 * dispatches commands from warmer_display_state_t.text into these
 * calls. See DISPLAY_MODE_TEXT in display.h.
 */
#ifndef TEXT_CONSOLE_H
#define TEXT_CONSOLE_H

#include <stdint.h>

/* 128 / FONT_W = 21 cols, 160 / FONT_H = 16 rows (font_bitmaps.h's
 * FONT_W/FONT_H are 6x10) - fits the screen exactly, 2px to spare on
 * the right, none top-to-bottom. 10px tall (up from an initial 8px)
 * makes the glyphs noticeably rounder/clearer at no cost to line
 * width - see the design discussion. */
#define TEXT_COLS 21
#define TEXT_ROWS 16

/* Clear the whole screen to `bg`. Does not touch the cursor row -
 * callers (display.c) reset that themselves alongside this. */
void text_console_clear(uint16_t bg);

/* Blit `line` at character row `row` (0-based, must be < TEXT_ROWS).
 * `line` is read up to TEXT_COLS bytes; a NUL ends it early and the
 * remainder of the row is blank-padded so a shorter new line fully
 * erases whatever longer line was there before. Bytes outside the
 * baked font range are substituted with '?'. `bg` is unused (glyph
 * bitmaps are full-bleed) but kept for call-site symmetry.
 */
void text_console_write_row(uint8_t row, const char *line, uint16_t bg);

#endif /* TEXT_CONSOLE_H */
