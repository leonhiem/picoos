/*
 * display.c - Display state machine
 *
 * Core idea: the main loop calls display_update() with a snapshot of
 * the warmer state. We compare against last-rendered state and only
 * repaint things that changed. Blink phase is driven by a single
 * timer so everything blinks in sync.
 */
#include "display.h"

#include <string.h>

#include "st7735.h"
#include "gfx.h"
#include "icons.h"
#include "baby.h"
#include "heat_indicator.h"
#include "apgar_timer.h"
#include "text_console.h"
#include "cue_icons.h"

/* ---- layout constants ---- */
#define BG_COLOR              COLOR_BLACK

#define ICON_COL_X            0       /* icon column starts at left edge */
#define ICON_SLOT_H           40      /* 4 slots x 40 = 160 (full height) */
#define ICON_OFFSET_X         2       /* small inset from left edge */
#define ICON_OFFSET_Y         6       /* offset within slot */

/* Order per direction: FAIL, ALARM, SENSOR, GEAR (top to bottom) */
#define ICON_SLOT_WARNING     0
#define ICON_SLOT_ALARM       1
#define ICON_SLOT_SENSOR      2
#define ICON_SLOT_MODE        3

#define DIVIDER_X             33      /* grey vertical line, icons | content */

/* 2nd divider: horizontal, between the face row and the APGAR row -
 * touches the vertical divider at DIVIDER_X, so the two read as one
 * connected frame rather than a floating stray line. Sits ~5px (about
 * 1mm on this panel) above the timer, inside the face's 80x80 box's
 * own empty bottom margin (the visible circle stops well above this,
 * see baby.c) - not in the 2px gap right above the timer, that read as
 * touching the digits. Being inside the face's box means it has to be
 * redrawn whenever the face is, so it's folded into render_baby() too
 * (like THERM_CUE below) rather than drawn once in render_graphical_frame(). */
#define DIVIDER2_Y            136

#define FACE_CENTER_X         79
#define FACE_CENTER_Y         100     /* center, but a bit below screen middle */

/* Thermometer/clock cue icons: plain, neutral, permanent labels (not
 * status indicators) disambiguating "this row is about temperature"
 * (the face) from "this row is a clock reading" (the APGAR timer) -
 * see art/render_cue_icons.py. Positioned in the empty margin to the
 * right of each row's actual content - the face bitmap's visible
 * circle only fills the center of its 80x80 box, and the timer only
 * spans ~46px of its row, leaving room on both without shrinking
 * either. THERM_CUE overlaps the tail end of the face's 80x80 blit
 * box, so it's folded into render_baby() rather than drawn separately
 * - see the comment there. */
#define THERM_CUE_X            105
#define THERM_CUE_Y            90     /* (100 - 20/2): THERM_ICON_SIZE is 20 */
#define CLOCK_CUE_X             106
#define CLOCK_CUE_Y             TIMER_Y

/* heater bar + heat rays, centered above the face */
#define BAR_CX                79
#define BAR_Y                 14
#define BAR_W                 70
#define BAR_H                 8
#define RAYS_TOP_Y            27
#define RAYS_BOT_Y            58      /* just above the face (face top = 60) */

/* APGAR timer (MM:SS), centered below the face and above the bottom edge.
 * Face bottom sits at FACE_CENTER_Y + 40 = 140; screen bottom is 160,
 * leaving a 20px band - the digit glyphs are baked at 16px tall
 * (art/render_timer.py), so this centers with a couple px to spare. */
#define TIMER_CX              79
#define TIMER_Y               142

/* ---- blink timing ---- */
#define BLINK_PERIOD_WARNING_MS  500   /* slow */
#define BLINK_PERIOD_ALARM_MS    250   /* fast */

/* ---- internal state ---- */
typedef struct {
    bool                   initialized;
    warmer_display_state_t last;       /* last rendered state */
    bool                   blink_warn_phase;
    bool                   blink_alarm_phase;
    uint32_t               last_warn_ms;
    uint32_t               last_alarm_ms;

    /* APGAR clock, owned entirely by the display: the caller just
     * pulses state->apgar_start true on a button press (rising edge
     * (re)starts the clock at 0); everything else - elapsed time,
     * checkpoint flashing - is computed here. */
    bool                   apgar_running;
    uint32_t               apgar_start_ms;
    uint32_t               last_apgar_elapsed_s;

    /* Text-mode cursor: which row the next TEXT_CMD_WRITE lands on.
     * Owned entirely here - the caller only issues CLEAR/SEEK/WRITE
     * commands (see warmer_display_state_t.text), it never tracks
     * position itself. */
    uint8_t                text_cursor_row;
} display_ctx_t;

static display_ctx_t ctx;

/* ---- helpers ---- */

static int16_t slot_y(int slot_index) {
    return slot_index * ICON_SLOT_H + ICON_OFFSET_Y;
}

/* draw mode icon based on state */
static void render_mode_icon(const warmer_display_state_t *s) {
    int16_t x = ICON_COL_X + ICON_OFFSET_X;
    int16_t y = slot_y(ICON_SLOT_MODE);
    if (s->mode == MODE_AUTO) {
        icon_mode_auto(x, y, ICON_ON, BG_COLOR);
    } else {
        icon_mode_manual(x, y, ICON_ON, BG_COLOR);
    }
}

/* blinks in sync with the warning icon (same timer/phase) when the
 * sensor has a problem; otherwise stays quiet/off */
static void render_sensor_icon(const warmer_display_state_t *s, bool blink_phase) {
    int16_t x = ICON_COL_X + ICON_OFFSET_X;
    int16_t y = slot_y(ICON_SLOT_SENSOR);
    icon_state_t st;
    if (!s->sensor_connected) st = blink_phase ? ICON_ON : ICON_OFF;
    else                      st = ICON_OFF;
    icon_sensor(x, y, st, BG_COLOR);
}

/* warning icon: blinks yellow, but uses ICON_OFF (grey) on the off phase
 * so it looks like a flashing yellow vs grey, not yellow vs background.
 */
static void render_warning_icon(const warmer_display_state_t *s, bool blink_phase) {
    int16_t x = ICON_COL_X + ICON_OFFSET_X;
    int16_t y = slot_y(ICON_SLOT_WARNING);
    icon_state_t st;
    if (s->warning) st = blink_phase ? ICON_ON : ICON_OFF;
    else            st = ICON_OFF;
    icon_warning(x, y, st, BG_COLOR);
}

static void render_alarm_icon(const warmer_display_state_t *s, bool blink_phase) {
    int16_t x = ICON_COL_X + ICON_OFFSET_X;
    int16_t y = slot_y(ICON_SLOT_ALARM);
    icon_state_t st;
    if (s->alarm) st = blink_phase ? ICON_ON : ICON_OFF;
    else          st = ICON_OFF;
    icon_alarm(x, y, st, BG_COLOR);
}

static void render_baby(const warmer_display_state_t *s) {
    baby_draw(FACE_CENTER_X, FACE_CENTER_Y, s->baby, BG_COLOR);
    /* baby_draw() is a full 80x80 blit that overlaps both THERM_CUE and
     * DIVIDER2_Y (see the layout comments above), so redraw them every
     * time the face does or they get overwritten with black. */
    cue_icon_thermometer_draw(THERM_CUE_X, THERM_CUE_Y, BG_COLOR);
    gfx_hline(DIVIDER_X, DIVIDER2_Y, ST7735_WIDTH - DIVIDER_X, COLOR_DIM_GREY);
}

/* Static graphical-mode frame: the vertical divider plus the clock cue -
 * neither ever changes, but they need repainting whenever the screen is
 * wiped (display_init(), and re-entering GRAPHICAL mode from TEXT mode).
 * The horizontal divider and thermometer cue are NOT here - both are
 * folded into render_baby() instead, since they overlap the face's blit
 * box and must be redrawn on every face update, not just on a full wipe. */
static void render_graphical_frame(void) {
    gfx_vline(DIVIDER_X, 0, ST7735_HEIGHT, COLOR_DIM_GREY);
    cue_icon_clock_draw(CLOCK_CUE_X, CLOCK_CUE_Y, BG_COLOR);
}

static void render_heat_indicator(const warmer_display_state_t *s) {
    heat_indicator_update(BAR_CX, BAR_Y, BAR_W, BAR_H,
                           RAYS_TOP_Y, RAYS_BOT_Y,
                           s->heater_percent, s->heater_failed, BG_COLOR);
}

static void render_apgar_timer(uint32_t elapsed_s, bool blink_phase) {
    bool in_window = apgar_timer_in_checkpoint_window(elapsed_s);
    apgar_timer_draw(TIMER_CX, TIMER_Y, elapsed_s,
                      in_window && blink_phase, BG_COLOR);
}

/* ---- public API ---- */

void display_init(void) {
    st7735_init();
    st7735_fill_screen(BG_COLOR);

    render_graphical_frame();

    /* paint all icons greyed initially */
    warmer_display_state_t blank;
    memset(&blank, 0, sizeof(blank));
    blank.mode = MODE_AUTO;
    blank.baby = BABY_OK;
    render_mode_icon(&blank);
    render_sensor_icon(&blank, true);
    render_warning_icon(&blank, true);
    render_alarm_icon(&blank, true);
    render_baby(&blank);
    render_heat_indicator(&blank);
    render_apgar_timer(0, true);

    memset(&ctx, 0, sizeof(ctx));
    ctx.initialized       = true;
    ctx.last              = blank;
    ctx.blink_warn_phase  = true;
    ctx.blink_alarm_phase = true;
}

void display_update(const warmer_display_state_t *state, uint32_t now_ms) {
    if (!ctx.initialized) return;
    if (!state) return;

    /* ---- update blink phases ---- */
    bool warn_changed = false, alarm_changed = false;

    if ((uint32_t)(now_ms - ctx.last_warn_ms) >= BLINK_PERIOD_WARNING_MS) {
        ctx.last_warn_ms = now_ms;
        ctx.blink_warn_phase = !ctx.blink_warn_phase;
        warn_changed = true;
    }
    if ((uint32_t)(now_ms - ctx.last_alarm_ms) >= BLINK_PERIOD_ALARM_MS) {
        ctx.last_alarm_ms = now_ms;
        ctx.blink_alarm_phase = !ctx.blink_alarm_phase;
        alarm_changed = true;
    }

    /* ---- APGAR clock: real elapsed time, kept running no matter which
     * display mode is on screen (e.g. staff can flip to a text screen
     * and back without pausing or losing it) ---- */
    bool apgar_edge = state->apgar_start && !ctx.last.apgar_start;
    if (apgar_edge) {
        ctx.apgar_running  = true;
        ctx.apgar_start_ms = now_ms;
    }
    uint32_t apgar_elapsed_s = ctx.apgar_running
        ? (uint32_t)(now_ms - ctx.apgar_start_ms) / 1000
        : 0;
    bool apgar_in_window = apgar_timer_in_checkpoint_window(apgar_elapsed_s);

    /* ---- mode switch: wipe the screen and force a full repaint of
     * whichever mode we're entering. Needed because the dirty-tracking
     * below only looks at whether individual fields changed since the
     * last call - right after a switch, fields that happen to have
     * stayed the same would otherwise be (wrongly) treated as "already
     * on screen", leaving a blank display. ---- */
    bool screen_mode_changed = state->screen_mode != ctx.last.screen_mode;
    if (screen_mode_changed) {
        st7735_fill_screen(BG_COLOR);
        if (state->screen_mode == DISPLAY_MODE_GRAPHICAL) {
            render_graphical_frame();
            render_mode_icon(state);
            render_sensor_icon(state, ctx.blink_warn_phase);
            render_warning_icon(state, ctx.blink_warn_phase);
            render_alarm_icon(state, ctx.blink_alarm_phase);
            render_baby(state);
            render_heat_indicator(state);
            render_apgar_timer(apgar_elapsed_s, ctx.blink_warn_phase);
            ctx.last_apgar_elapsed_s = apgar_elapsed_s;
        } else {
            ctx.text_cursor_row = 0;
        }
    }

    if (state->screen_mode == DISPLAY_MODE_TEXT) {
        bool cmd_pending = state->text.seq != ctx.last.text.seq;
        if (cmd_pending) {
            switch (state->text.cmd) {
            case TEXT_CMD_CLEAR:
                text_console_clear(BG_COLOR);
                ctx.text_cursor_row = 0;
                break;
            case TEXT_CMD_SEEK:
                ctx.text_cursor_row = (state->text.row < TEXT_ROWS)
                                       ? state->text.row : (TEXT_ROWS - 1);
                break;
            case TEXT_CMD_WRITE:
                text_console_write_row(ctx.text_cursor_row, state->text.line, BG_COLOR);
                if (ctx.text_cursor_row + 1 < TEXT_ROWS) ctx.text_cursor_row++;
                break;
            case TEXT_CMD_NONE:
            default:
                break;
            }
        }
        ctx.last = *state;
        return;
    }

    /* ---- GRAPHICAL mode: dirty-tracked redraw of whatever changed.
     * Skipped the same tick screen_mode_changed fired, since that
     * already painted everything fresh above. ---- */
    if (!screen_mode_changed) {
        if (state->mode != ctx.last.mode) {
            render_mode_icon(state);
        }
        if (state->sensor_connected != ctx.last.sensor_connected ||
            (!state->sensor_connected && warn_changed)) {
            render_sensor_icon(state, ctx.blink_warn_phase);
        }
        if (state->warning != ctx.last.warning ||
            (state->warning && warn_changed)) {
            render_warning_icon(state, ctx.blink_warn_phase);
        }
        if (state->alarm != ctx.last.alarm ||
            (state->alarm && alarm_changed)) {
            render_alarm_icon(state, ctx.blink_alarm_phase);
        }

        if (state->baby != ctx.last.baby) {
            render_baby(state);
        }

        if (state->heater_percent != ctx.last.heater_percent ||
            state->heater_failed  != ctx.last.heater_failed) {
            render_heat_indicator(state);
        }

        if (apgar_edge ||
            apgar_elapsed_s != ctx.last_apgar_elapsed_s ||
            (apgar_in_window && warn_changed)) {
            render_apgar_timer(apgar_elapsed_s, ctx.blink_warn_phase);
            ctx.last_apgar_elapsed_s = apgar_elapsed_s;
        }
    }

    ctx.last = *state;
}
