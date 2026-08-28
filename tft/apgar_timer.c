/*
 * apgar_timer.c - Blits the 5 fixed-size glyph cells (MM:SS) that make up
 * the APGAR timer, and flashes it as a solid black-on-yellow block around
 * the standard APGAR checkpoints. See apgar_timer.h and
 * input/art/render_timer.py.
 */
#include "apgar_timer.h"
#include "st7735.h"
#include "digit_bitmaps.h"

#include <stddef.h>

/* Real APGAR checkpoints: scored at 1 and 5 minutes after birth always,
 * at 10 minutes too if the baby's score is still low. Flash the readout
 * for a short window at each so it reads as "score now", not a plain
 * clock.
 *
 * APGAR_FAST_DEMO shrinks these to seconds instead of minutes, purely so
 * the flash behavior can be previewed on the bench in under a minute
 * instead of waiting 10+ real minutes - do NOT ship a real unit with
 * this defined. */
#ifndef APGAR_FAST_DEMO
#define APGAR_CHECKPOINT_1_S    60   /* 1 min */
#define APGAR_CHECKPOINT_2_S   300   /* 5 min */
#define APGAR_CHECKPOINT_3_S   600   /* 10 min */
#define APGAR_FLASH_WINDOW_S    15
#else
#define APGAR_CHECKPOINT_1_S    10
#define APGAR_CHECKPOINT_2_S    25
#define APGAR_CHECKPOINT_3_S    40
#define APGAR_FLASH_WINDOW_S     5
#endif

static const uint32_t CHECKPOINTS[] = {
    APGAR_CHECKPOINT_1_S,
    APGAR_CHECKPOINT_2_S,
    APGAR_CHECKPOINT_3_S,
};
#define N_CHECKPOINTS (sizeof(CHECKPOINTS) / sizeof(CHECKPOINTS[0]))

bool apgar_timer_in_checkpoint_window(uint32_t total_seconds) {
    for (size_t i = 0; i < N_CHECKPOINTS; i++) {
        uint32_t cp = CHECKPOINTS[i];
        if (total_seconds >= cp && total_seconds < cp + APGAR_FLASH_WINDOW_S) {
            return true;
        }
    }
    return false;
}

static const uint16_t *DIGITS[10] = {
    digit_0, digit_1, digit_2, digit_3, digit_4,
    digit_5, digit_6, digit_7, digit_8, digit_9,
};
static const uint16_t *DIGITS_HI[10] = {
    digit_0_hi, digit_1_hi, digit_2_hi, digit_3_hi, digit_4_hi,
    digit_5_hi, digit_6_hi, digit_7_hi, digit_8_hi, digit_9_hi,
};

void apgar_timer_draw(int16_t cx, int16_t y, uint32_t total_seconds,
                       bool highlight, uint16_t bg) {
    (void)bg; /* glyph bitmaps are full-bleed (black bg baked in) */

    if (total_seconds > 99 * 60 + 59) total_seconds = 99 * 60 + 59; /* cap at 99:59 */
    uint32_t mm = total_seconds / 60;
    uint32_t ss = total_seconds % 60;

    const uint16_t *const *digits = highlight ? DIGITS_HI : DIGITS;
    const uint16_t *colon = highlight ? digit_colon_hi : digit_colon;

    int16_t total_w = DIGIT_W * 4 + COLON_W;
    int16_t x = cx - total_w / 2;

    st7735_draw_bitmap(x, y, DIGIT_W, DIGIT_H, digits[mm / 10]); x += DIGIT_W;
    st7735_draw_bitmap(x, y, DIGIT_W, DIGIT_H, digits[mm % 10]); x += DIGIT_W;
    st7735_draw_bitmap(x, y, COLON_W, DIGIT_H, colon);           x += COLON_W;
    st7735_draw_bitmap(x, y, DIGIT_W, DIGIT_H, digits[ss / 10]); x += DIGIT_W;
    st7735_draw_bitmap(x, y, DIGIT_W, DIGIT_H, digits[ss % 10]);
}
