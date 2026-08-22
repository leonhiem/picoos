/*
 * heat_indicator.c - Resistor-style heater element + radiating heat rays.
 * See heat_indicator.h for the design rationale.
 */
#include "heat_indicator.h"
#include "gfx.h"
#include "st7735.h"

#include <math.h>

#define N_RAYS    5
#define RAY_SEGS  12
#define RAY_AMP   2.2f
#define TWO_PI    6.28318530718f

/* 6-stop palette across heater percent, per direction: grey (idle, same
 * grey as an "off" icon) -> light grey -> dark red -> red -> dark orange
 * -> orange (full power). Linearly interpolated between stops. */
typedef struct { uint8_t p, r, g, b; } heat_stop_t;

static const heat_stop_t STOPS[] = {
    {  0,  32,  32,  32 },
    { 20, 110, 110, 110 },
    { 40, 120,  20,  20 },
    { 60, 215,  35,  35 },
    { 80, 180,  90,  10 },
    {100, 255, 150,   0 },
};
#define N_STOPS (sizeof(STOPS) / sizeof(STOPS[0]))

static uint16_t heat_color_for_percent(uint8_t percent) {
    if (percent > 100) percent = 100;
    for (int i = 0; i < (int)N_STOPS - 1; i++) {
        const heat_stop_t *a = &STOPS[i];
        const heat_stop_t *b = &STOPS[i + 1];
        if (percent >= a->p && percent <= b->p) {
            int span = b->p - a->p;
            int t = span ? (percent - a->p) : 0;
            int r = a->r + (b->r - a->r) * t / span;
            int g = a->g + (b->g - a->g) * t / span;
            int bl = a->b + (b->b - a->b) * t / span;
            return RGB565(r, g, bl);
        }
    }
    return RGB565(STOPS[N_STOPS - 1].r, STOPS[N_STOPS - 1].g, STOPS[N_STOPS - 1].b);
}

/* capsule = two filled circles (end caps) + a filled rect between them */
static void draw_capsule(int16_t cx, int16_t cy, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    int16_t r = h / 2;
    int16_t rect_w = w - h;
    if (rect_w < 0) rect_w = 0;
    gfx_fill_circle(cx - rect_w / 2, cy, r, color);
    gfx_fill_circle(cx + rect_w / 2, cy, r, color);
    if (rect_w > 0) gfx_fill_rect(cx - rect_w / 2, cy - r, rect_w, h, color);
}

void heat_indicator_update(int16_t elem_cx, int16_t elem_y, int16_t elem_w, int16_t elem_h,
                            int16_t rays_top_y, int16_t rays_bot_y,
                            uint8_t percent, bool heater_failed, uint16_t bg) {
    if (percent > 100) percent = 100;

    /* clear the whole bounding box (element + rays), a little wider than
     * the element itself so ray wobble never leaves stale pixels */
    int16_t clear_x0 = elem_cx - elem_w / 2 - 4;
    int16_t clear_w  = elem_w + 8;
    gfx_fill_rect(clear_x0, elem_y, clear_w, rays_bot_y - elem_y, bg);

    uint16_t color = heater_failed ? COLOR_RED : heat_color_for_percent(percent);

    int16_t body_w = (elem_w * 55) / 100;
    int16_t cy = elem_y + elem_h / 2;

    /* wire leads (silver), from the bounding box edges in to the body */
    gfx_hline(elem_cx - elem_w / 2, cy, (elem_w - body_w) / 2, COLOR_LIGHT_GREY);
    gfx_hline(elem_cx + body_w / 2, cy, (elem_w - body_w) / 2, COLOR_LIGHT_GREY);

    /* body: outline capsule, then fill capsule on top */
    draw_capsule(elem_cx, cy, body_w + 2, elem_h + 2, COLOR_LIGHT_GREY);
    draw_capsule(elem_cx, cy, body_w, elem_h, color);

    if (heater_failed) return; /* fault, not genuine heat: rays off */

    /* Same "stretched S" wave shape (one full sine period) for every ray -
     * only x position differs. All 5 always draw; only the color (shared
     * with the element) changes with percent. */
    static const float ray_offset_ratio[N_RAYS] = { -0.36f, -0.19f, 0.0f, 0.19f, 0.36f };
    int16_t ray_span = rays_bot_y - rays_top_y;

    for (int i = 0; i < N_RAYS; i++) {
        int16_t rx = elem_cx + (int16_t)(ray_offset_ratio[i] * elem_w);
        int16_t prev_x = rx, prev_y = rays_top_y;
        for (int s = 1; s <= RAY_SEGS; s++) {
            float t = (float)s / RAY_SEGS;
            int16_t y = rays_top_y + (int16_t)(t * ray_span);
            int16_t x = rx + (int16_t)(RAY_AMP * sinf(TWO_PI * t));
            gfx_line(prev_x, prev_y, x, y, color);
            prev_x = x;
            prev_y = y;
        }
    }
}
