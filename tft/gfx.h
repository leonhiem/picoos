/*
 * gfx.h - Geometric drawing primitives built on top of st7735.c
 *
 * Pure-C, no allocations, no globals beyond what st7735 already uses.
 * All routines clip to the display bounds, so off-screen coordinates
 * are safe (just discarded).
 */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "st7735.h"

/* Single-pixel — direct passthrough to the driver */
static inline void gfx_pixel(int16_t x, int16_t y, uint16_t c) {
    st7735_draw_pixel(x, y, c);
}

/* Rectangles */
void gfx_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void gfx_rect     (int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/* Lines */
void gfx_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void gfx_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
void gfx_line (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void gfx_thick_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    int16_t thickness, uint16_t color);

/* Circles & ellipses (Bresenham) */
void gfx_circle      (int16_t cx, int16_t cy, int16_t r, uint16_t color);
void gfx_fill_circle (int16_t cx, int16_t cy, int16_t r, uint16_t color);
void gfx_fill_ellipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color);

/* Filled triangle (used for warning ⚠) */
void gfx_fill_triangle(int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1,
                       int16_t x2, int16_t y2,
                       uint16_t color);

#endif /* GFX_H */
