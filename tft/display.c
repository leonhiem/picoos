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

#define FACE_CENTER_X         79
#define FACE_CENTER_Y         100     /* center, but a bit below screen middle */

/* heater bar + heat rays, centered above the face */
#define BAR_CX                79
#define BAR_Y                 14
#define BAR_W                 70
#define BAR_H                 8
#define RAYS_TOP_Y            27
#define RAYS_BOT_Y            58      /* just above the face (face top = 60) */

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
}

static void render_heat_indicator(const warmer_display_state_t *s) {
    heat_indicator_update(BAR_CX, BAR_Y, BAR_W, BAR_H,
                           RAYS_TOP_Y, RAYS_BOT_Y,
                           s->heater_percent, s->heater_failed, BG_COLOR);
}

/* ---- public API ---- */

void display_init(void) {
    st7735_init();
    st7735_fill_screen(BG_COLOR);

    /* static divider between the icon column and the content area - kept
     * dim/near-black, same grey as an "off" icon, so it stays subtle */
    gfx_vline(DIVIDER_X, 0, ST7735_HEIGHT, COLOR_DIM_GREY);

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

    /* ---- compare state and redraw what changed ---- */
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

    ctx.last = *state;
}
