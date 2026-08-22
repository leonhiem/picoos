/*
 * gfx.c - Geometric drawing primitives
 */
#include "gfx.h"

#include <stdlib.h>

/* helpers */
static inline int16_t i_min(int16_t a, int16_t b) { return a < b ? a : b; }
static inline int16_t i_max(int16_t a, int16_t b) { return a > b ? a : b; }
static inline void    i_swap(int16_t *a, int16_t *b) { int16_t t = *a; *a = *b; *b = t; }
static inline int16_t i_abs(int16_t a) { return a < 0 ? -a : a; }

/* ---- rectangle ---- */
void gfx_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    st7735_fill_rect(x, y, w, h, color);
}

void gfx_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    st7735_fill_rect(x,         y,         w, 1, color);
    st7735_fill_rect(x,         y + h - 1, w, 1, color);
    st7735_fill_rect(x,         y,         1, h, color);
    st7735_fill_rect(x + w - 1, y,         1, h, color);
}

/* ---- horizontal / vertical lines (fast) ---- */
void gfx_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (w < 0) { x += w; w = -w; }
    st7735_fill_rect(x, y, w, 1, color);
}

void gfx_vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (h < 0) { y += h; h = -h; }
    st7735_fill_rect(x, y, 1, h, color);
}

/* ---- generic line (Bresenham) ---- */
void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t steep = i_abs(y1 - y0) > i_abs(x1 - x0);
    if (steep) { i_swap(&x0, &y0); i_swap(&x1, &y1); }
    if (x0 > x1) { i_swap(&x0, &x1); i_swap(&y0, &y1); }

    int16_t dx = x1 - x0;
    int16_t dy = i_abs(y1 - y0);
    int16_t err = dx / 2;
    int16_t ystep = (y0 < y1) ? 1 : -1;
    int16_t y = y0;

    for (int16_t x = x0; x <= x1; x++) {
        if (steep) gfx_pixel(y, x, color);
        else       gfx_pixel(x, y, color);
        err -= dy;
        if (err < 0) { y += ystep; err += dx; }
    }
}

/* ---- thick line: draw n parallel lines perpendicular to direction ---- */
void gfx_thick_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    int16_t thickness, uint16_t color) {
    if (thickness < 1) thickness = 1;
    /* simple approach: stamp a small filled rect at each Bresenham point.
       Looks slightly chunky but is fast and bug-free. */
    int16_t r = thickness / 2;

    int16_t steep = i_abs(y1 - y0) > i_abs(x1 - x0);
    if (steep) { i_swap(&x0, &y0); i_swap(&x1, &y1); }
    if (x0 > x1) { i_swap(&x0, &x1); i_swap(&y0, &y1); }

    int16_t dx = x1 - x0;
    int16_t dy = i_abs(y1 - y0);
    int16_t err = dx / 2;
    int16_t ystep = (y0 < y1) ? 1 : -1;
    int16_t y = y0;

    for (int16_t x = x0; x <= x1; x++) {
        if (steep) st7735_fill_rect(y - r, x - r, thickness, thickness, color);
        else       st7735_fill_rect(x - r, y - r, thickness, thickness, color);
        err -= dy;
        if (err < 0) { y += ystep; err += dx; }
    }
}

/* ---- circle outline (Bresenham midpoint algorithm) ---- */
void gfx_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (r <= 0) { gfx_pixel(cx, cy, color); return; }

    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    gfx_pixel(cx,     cy + r, color);
    gfx_pixel(cx,     cy - r, color);
    gfx_pixel(cx + r, cy,     color);
    gfx_pixel(cx - r, cy,     color);

    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++;
        ddF_x += 2;
        f += ddF_x;

        gfx_pixel(cx + x, cy + y, color);
        gfx_pixel(cx - x, cy + y, color);
        gfx_pixel(cx + x, cy - y, color);
        gfx_pixel(cx - x, cy - y, color);
        gfx_pixel(cx + y, cy + x, color);
        gfx_pixel(cx - y, cy + x, color);
        gfx_pixel(cx + y, cy - x, color);
        gfx_pixel(cx - y, cy - x, color);
    }
}

/* ---- filled circle: draw horizontal lines per row ---- */
void gfx_fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (r <= 0) { gfx_pixel(cx, cy, color); return; }

    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    gfx_hline(cx - r, cy, 2 * r + 1, color);

    while (x < y) {
        if (f >= 0) {
            gfx_hline(cx - x, cy + y, 2 * x + 1, color);
            gfx_hline(cx - x, cy - y, 2 * x + 1, color);
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        gfx_hline(cx - y, cy + x, 2 * y + 1, color);
        gfx_hline(cx - y, cy - x, 2 * y + 1, color);
    }
}

/* ---- filled ellipse: simple but correct
 * Uses the rasterization rule x^2/rx^2 + y^2/ry^2 <= 1, scanned per row.
 */
void gfx_fill_ellipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color) {
    if (rx <= 0 || ry <= 0) return;
    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;

    for (int16_t dy = -ry; dy <= ry; dy++) {
        /* solve x^2 <= rx2 * (1 - dy^2 / ry2) -> x <= sqrt(rx2 - rx2*dy^2/ry2) */
        int32_t term = rx2 - (rx2 * (int32_t)dy * (int32_t)dy) / ry2;
        if (term < 0) continue;
        /* integer sqrt — loop is fine, max rx ≈ 60 */
        int16_t dx = 0;
        while ((int32_t)(dx + 1) * (dx + 1) <= term) dx++;
        gfx_hline(cx - dx, cy + dy, 2 * dx + 1, color);
    }
}

/* ---- filled triangle (Adafruit GFX-style scanline approach) ---- */
void gfx_fill_triangle(int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1,
                       int16_t x2, int16_t y2,
                       uint16_t color) {
    /* sort by y */
    if (y0 > y1) { i_swap(&y0, &y1); i_swap(&x0, &x1); }
    if (y1 > y2) { i_swap(&y2, &y1); i_swap(&x2, &x1); }
    if (y0 > y1) { i_swap(&y0, &y1); i_swap(&x0, &x1); }

    if (y0 == y2) {
        /* degenerate horizontal */
        int16_t xs = i_min(x0, i_min(x1, x2));
        int16_t xe = i_max(x0, i_max(x1, x2));
        gfx_hline(xs, y0, xe - xs + 1, color);
        return;
    }

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    int16_t y;
    /* upper part: y0 to y1 (or y2 if y0==y1) */
    int16_t last = (y1 == y2) ? y1 : (y1 - 1);

    for (y = y0; y <= last; y++) {
        int16_t a = x0 + (int16_t)(sa / dy01);
        int16_t b = x0 + (int16_t)(sb / dy02);
        sa += dx01;
        sb += dx02;
        if (a > b) i_swap(&a, &b);
        gfx_hline(a, y, b - a + 1, color);
    }

    /* lower part: y1 to y2 */
    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        int16_t a = x1 + (int16_t)(sa / dy12);
        int16_t b = x0 + (int16_t)(sb / dy02);
        sa += dx12;
        sb += dx02;
        if (a > b) i_swap(&a, &b);
        gfx_hline(a, y, b - a + 1, color);
    }
}
