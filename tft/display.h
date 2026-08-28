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
 * Two display modes, selected by `screen_mode` on the state struct:
 *
 *   DISPLAY_MODE_GRAPHICAL (0, default) - the warmer UI below.
 *   DISPLAY_MODE_TEXT      (1)          - a full-screen 21x16 fixed-font
 *                                         ASCII console, see `text` below
 *                                         and text_console.h.
 *
 * Graphical layout (128x160 portrait):
 *
 *   icon column on LEFT     (x = 0..30, full height, 4 icons:
 *                            FAIL / ALARM / SENSOR / MODE top to bottom)
 *   grey vertical divider   (x = 33)
 *   heater bar + heat rays  (centered above the face)
 *   baby face (emoji)       (centered, a bit below screen middle)
 *   APGAR timer (MM:SS)     (centered, between the face and the bottom edge)
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#include "baby.h"
#include "text_console.h"

typedef enum {
    MODE_AUTO   = 0,
    MODE_MANUAL = 1,
} warmer_mode_t;

/* Which of the two display modes is on screen. 0 = graphical is the
 * default so that any caller which doesn't yet know about text mode
 * (a zero-initialized or partially-filled state struct) keeps getting
 * today's graphical UI, unchanged - this field is purely additive. */
typedef enum {
    DISPLAY_MODE_GRAPHICAL = 0,
    DISPLAY_MODE_TEXT      = 1,
} display_mode_t;

/* Text-mode commands, dispatched from `warmer_display_state_t.text`.
 * Only one command can be "in flight" per struct - bump `seq` to fire
 * whatever `cmd` currently says; display.c edge-detects the seq change
 * the same way it already does for apgar_start. */
typedef enum {
    TEXT_CMD_NONE  = 0,
    TEXT_CMD_CLEAR = 1,   /* clear the whole text screen, cursor -> row 0 */
    TEXT_CMD_SEEK  = 2,   /* move the cursor to `.row` */
    TEXT_CMD_WRITE = 3,   /* write `.line` at the cursor row, then cursor++ */
} text_cmd_t;

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
    bool         apgar_start;        /* momentary trigger (e.g. a push
                                       * button): a rising edge (re)starts
                                       * the APGAR MM:SS clock at 0. The
                                       * display owns the clock itself -
                                       * caller doesn't compute elapsed
                                       * time, just pulses this true for
                                       * one call on button-press. Keeps
                                       * running regardless of
                                       * `screen_mode`. */

    display_mode_t screen_mode;      /* DISPLAY_MODE_GRAPHICAL (default)
                                       * or DISPLAY_MODE_TEXT - see above.
                                       * (Named separately from `mode`
                                       * above, which is auto/manual.) */
    struct {
        text_cmd_t cmd;               /* which command `seq` fires */
        uint32_t   seq;               /* bump to fire `cmd` (edge-detected
                                        * against the previous call, same
                                        * pattern as apgar_start) */
        uint8_t    row;                /* TEXT_CMD_SEEK's target row */
        char       line[TEXT_COLS + 1]; /* TEXT_CMD_WRITE's payload - NUL-
                                        * terminated; extra bytes past the
                                        * NUL are ignored, missing columns
                                        * are blank-padded on screen */
    } text;                          /* only used while screen_mode == DISPLAY_MODE_TEXT */
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
