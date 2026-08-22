/*
 * display.h - High-level display state machine.
 *
 * Holds a "warmer state" struct and renders it. Designed to be
 * called from the main loop at any rate; it tracks what's actually
 * on screen and only redraws what changed (dirty tracking).
 *
 * Blinking is driven by a single boolean toggled internally by a
 * timer, so all blinking icons stay in sync.
 *
 * Layout (128x160 portrait):
 *
 *   icon column on LEFT     (x = 0..30, full height, 4 icons:
 *                            FAIL / ALARM / SENSOR / MODE top to bottom)
 *   grey vertical divider   (x = 33)
 *   heater bar + heat rays  (centered above the face)
 *   baby face (emoji)       (centered, a bit below screen middle)
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#include "baby.h"

typedef enum {
    MODE_AUTO   = 0,
    MODE_MANUAL = 1,
} warmer_mode_t;

/* Current state of the warmer that the display reflects.
 * The main loop fills this in, then calls display_update().
 */
typedef struct {
    warmer_mode_t mode;
    bool         heater_on;          /* heater currently outputting */
    bool         heater_failed;      /* heater fault detected */
    bool         sensor_connected;   /* skin sensor in place */
    bool         warning;            /* yellow triangle (safe mode) */
    bool         alarm;              /* red bell (urgent) */
    uint8_t      heater_percent;     /* 0..100 PID output */
    baby_state_t baby;               /* baby color/expression */
} warmer_display_state_t;

/* Initialize the display layer (calls st7735_init internally
 * and paints the static frame). */
void display_init(void);

/* Call this often (e.g. every main-loop tick).
 * It will only do work if state changed or blink phase changed.
 *
 * `now_ms` is the system millisecond counter — use the same
 * monotonic clock everywhere (e.g. to_ms_since_boot).
 */
void display_update(const warmer_display_state_t *state, uint32_t now_ms);

#endif /* DISPLAY_H */
