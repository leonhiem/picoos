/**
 * dev/tft.cpp — /dev/tft/*: ST7735 front-panel display
 *
 * Mirrors dev/seg7.cpp's /dev/leds/<name> shape: independent
 * read/write devices sharing one piece of shadow state. Unlike
 * dev/seg7.cpp's shift register though, there's no shiftregister here
 * and no hardware reason to push a frame out synchronously on every
 * write -- /dev/tft/* writes here just land in the shadow fields below.
 * The display itself needs *continuous* redraws regardless of whether
 * anything changed (blink timing), so that's handled separately by
 * tft_flush_task(), registered directly with kernel/task.h in
 * picoos.cpp (task_register("tftflush", ...), not a killable job slot)
 * -- see the header comment there for why it has to be a task and not
 * a job.
 *
 * Same seven conditions bin/ledwire already drives into /dev/leds/*,
 * with `warm` dropped (redundant with heater's own color -- see
 * tft-picoos-integration-spec.md) and `heater` added (0-100, a
 * continuous value with no LED equivalent):
 *
 *   aut/man  -- mirrors /dev/heaterauto (on/off)
 *   chk      -- on during /dev/state == safe
 *   low/high -- mirrors /dev/alarm/{templow,temphigh}
 *   fail     -- mirrors /dev/alarm/heater
 *   heater   -- mirrors /dev/heater (0-100)
 *
 * Plus one more, added 2026-08-28 alongside the display's new APGAR
 * clock:
 *
 *   apgar    -- raw forward of /dev/buttons/start (via prog/tftwire),
 *               not derived/computed here -- see the apgar_start note
 *               below.
 *
 * And four more, added 2026-08-29 alongside the display's new text
 * console mode (see tft-picoos-integration-spec.md and st7735's
 * HANDOFF.md):
 *
 *   mode  -- read/write "graphical"/"text", selects screen_mode.
 *            Persistent state, like aut/man/etc above -- not a
 *            one-shot command, so it gets a real read() (echoes the
 *            current mode) unlike the three below.
 *   seek  -- write a row number 0-15 (text_console.h's TEXT_ROWS),
 *            moves the text cursor there. write-only (0, seg7big-
 *            style), and rejected (-1) unless mode is currently
 *            "text" -- display.c already no-ops text commands in
 *            graphical mode internally (the whole TEXT_CMD_* dispatch
 *            in display_update() is gated on screen_mode==TEXT), this
 *            just makes that same rule visible at the write() call
 *            site instead of silently swallowing it one layer down.
 *   text  -- write a line; lands at the cursor row, which then
 *            auto-advances (display.c's own doing, mirrors a real
 *            file's write-advances-the-offset behavior -- seek is
 *            this device's lseek()). bin/echo already writes here via
 *            the shell's `>` redirect, e.g.
 *            `echo Heater: 42 % > /dev/tft/text`, no new plumbing
 *            needed. Same mode gating as seek.
 *   clear -- write anything, wipes the text screen and homes the
 *            cursor to row 0. Same mode gating as seek/text -- entering
 *            text mode from graphical already wipes+homes on its own
 *            (display.c), this is for clearing a stale screen without
 *            leaving text mode.
 *
 * The actual rendering lives entirely behind tft/display.h's blackbox
 * API (display_init()/display_update()) -- this file and prog/tftwire
 * are the only two picoos files that touch it, and neither ever reaches
 * past display.h into st7735.c/gfx.c/etc directly.
 *
 * fail's red<->grey rod blink: heat_indicator.c's heater_failed only
 * ever renders solid red, no blink of its own, and touching the
 * vendored driver files was deliberately avoided (see the integration
 * spec). So tft_flush_task alternates what it feeds in instead: on one
 * phase heater_failed=true (solid red), on the other heater_percent=0
 * with heater_failed=false (grey -- the same color the 0% stop already
 * uses), toggling on the same ~500ms period display.c's own
 * warning-icon blink uses internally. Approximate sync (two independent
 * timers), not pixel-exact -- good enough, per Leon.
 */
#include "warmer.h"
#include "kernel/fs.h"
extern "C" {
#include "tft/display.h" // vendored C driver -- see tft/README.md; this
                          // extern "C" is pure interop boilerplate (the
                          // .c files link with C names, this file is
                          // C++), not a change to the driver itself.
}
#include "pico/time.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool    aut = true, man = false, chk = false;
static bool    low = false, high = false, fail = false;
static uint8_t heater = 0;
static bool    apgar = false; // raw button forward, not a condition --
                               // display.c does its own rising-edge
                               // detection (see tft_flush_task below),
                               // so this can just be whatever tftwire
                               // last wrote, on or off.

// Text console mode + command staging -- see the header comment's
// mode/seek/text/clear entries. text_cmd/text_seq/text_row/text_line
// mirror warmer_display_state_t.text's fields one-for-one (that struct
// is anonymous in display.h, so it can't just be reused by name here).
static display_mode_t screen_mode = DISPLAY_MODE_GRAPHICAL;
static text_cmd_t     text_cmd    = TEXT_CMD_NONE;
static uint32_t        text_seq   = 0;
static uint8_t          text_row  = 0;
static char             text_line[TEXT_COLS + 1] = "";

#define FAIL_BLINK_MS 500
static bool            fail_blink_phase = false;
static absolute_time_t fail_blink_next;

/* ═══════════════════════════════════════════════════
   /dev/tft/<name> -- twelve independent devices, one per shadow field,
   same idea (and same hand-duplication tradeoff, fine at twelve) as
   dev/seg7.cpp's /dev/leds/<name>. mode/seek/text/clear (added
   2026-08-29) are the odd ones out: not simple on/off/numeric fields
   but a small state machine mirroring display.h's text_cmd_t -- see
   the header comment above.
   ═══════════════════════════════════════════════════ */
static int bool_read(bool v, char *buf, int len)
{
    return snprintf(buf, len, v ? "on\n" : "off\n");
}

static int bool_write(bool *field, const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { *field = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { *field = false; return len; }
    return -1;
}

static int aut_read(char *buf, int len)         { return bool_read(aut, buf, len); }
static int aut_write(const char *buf, int len)  { return bool_write(&aut, buf, len); }
static int man_read(char *buf, int len)         { return bool_read(man, buf, len); }
static int man_write(const char *buf, int len)  { return bool_write(&man, buf, len); }
static int chk_read(char *buf, int len)         { return bool_read(chk, buf, len); }
static int chk_write(const char *buf, int len)  { return bool_write(&chk, buf, len); }
static int low_read(char *buf, int len)         { return bool_read(low, buf, len); }
static int low_write(const char *buf, int len)  { return bool_write(&low, buf, len); }
static int high_read(char *buf, int len)        { return bool_read(high, buf, len); }
static int high_write(const char *buf, int len) { return bool_write(&high, buf, len); }
static int fail_read(char *buf, int len)        { return bool_read(fail, buf, len); }
static int fail_write(const char *buf, int len) { return bool_write(&fail, buf, len); }
static int apgar_read(char *buf, int len)         { return bool_read(apgar, buf, len); }
static int apgar_write(const char *buf, int len)  { return bool_write(&apgar, buf, len); }

static int heater_read(char *buf, int len)
{
    return snprintf(buf, len, "%d\n", heater);
}

static int heater_write(const char *buf, int len)
{
    int v = atoi(buf);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    heater = (uint8_t)v;
    return len;
}

static int mode_read(char *buf, int len)
{
    return snprintf(buf, len, screen_mode == DISPLAY_MODE_TEXT ? "text\n" : "graphical\n");
}

static int mode_write(const char *buf, int len)
{
    if (len >= 4 && strncmp(buf, "text", 4) == 0)          { screen_mode = DISPLAY_MODE_TEXT;      return len; }
    if (len >= 9 && strncmp(buf, "graphical", 9) == 0)     { screen_mode = DISPLAY_MODE_GRAPHICAL; return len; }
    return -1;
}

// seek/text/clear are write-only commands (0 for read, seg7big-style)
// and only make sense once mode is "text" -- display.c already no-ops
// TEXT_CMD_* while screen_mode is graphical (the whole dispatch block
// in display_update() is gated on it), so rejecting here just surfaces
// that same rule at the write() call site instead of silently
// swallowing it a layer down.
static int seek_write(const char *buf, int len)
{
    if (screen_mode != DISPLAY_MODE_TEXT) return -1;
    int v = atoi(buf);
    if (v < 0)              v = 0;
    if (v >= TEXT_ROWS)     v = TEXT_ROWS - 1;
    text_row = (uint8_t)v;
    text_cmd = TEXT_CMD_SEEK;
    text_seq++;
    return len;
}

// Mirrors a real file's write-advances-the-offset behavior: the cursor
// this lands at auto-advances afterward (display.c's doing, not
// tracked here) -- seek above is this device's lseek(). bin/echo
// already writes here via the shell's `>` redirect with no new
// plumbing, e.g. `echo Heater: 42 % > /dev/tft/text`.
static int text_write(const char *buf, int len)
{
    if (screen_mode != DISPLAY_MODE_TEXT) return -1;
    int n = len;
    if (n > TEXT_COLS) n = TEXT_COLS;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--; // echo appends '\n'
    memcpy(text_line, buf, n);
    text_line[n] = '\0';
    text_cmd = TEXT_CMD_WRITE;
    text_seq++;
    return len;
}

static int text_read(char *buf, int len)
{
    return snprintf(buf, len, "%s\n", text_line);
}

// Wipes the text screen and homes the cursor without leaving text mode
// (entering text mode from graphical already does both on its own, in
// display.c). Argument ignored -- any write fires it, same as
// dev/seg7.cpp's whole-panel writes take a fixed shape, not a value.
static int clear_write(const char *buf, int len)
{
    (void)buf;
    if (screen_mode != DISPLAY_MODE_TEXT) return -1;
    text_cmd = TEXT_CMD_CLEAR;
    text_seq++;
    return len;
}

static const device_t dev_tft_aut    = {"/dev/tft/aut",    0, 0, aut_read,    aut_write};
static const device_t dev_tft_man    = {"/dev/tft/man",    0, 0, man_read,    man_write};
static const device_t dev_tft_chk    = {"/dev/tft/chk",    0, 0, chk_read,    chk_write};
static const device_t dev_tft_low    = {"/dev/tft/low",    0, 0, low_read,    low_write};
static const device_t dev_tft_high   = {"/dev/tft/high",   0, 0, high_read,   high_write};
static const device_t dev_tft_fail   = {"/dev/tft/fail",   0, 0, fail_read,   fail_write};
static const device_t dev_tft_heater = {"/dev/tft/heater", 0, 0, heater_read, heater_write};
static const device_t dev_tft_apgar  = {"/dev/tft/apgar",  0, 0, apgar_read,  apgar_write};
static const device_t dev_tft_mode   = {"/dev/tft/mode",   0, 0, mode_read,   mode_write};
static const device_t dev_tft_seek   = {"/dev/tft/seek",   0, 0, 0,           seek_write};
static const device_t dev_tft_text   = {"/dev/tft/text",   0, 0, text_read,   text_write};
static const device_t dev_tft_clear  = {"/dev/tft/clear",  0, 0, 0,           clear_write};

void tft_devices_register(void)
{
    fs_register(&dev_tft_aut);
    fs_register(&dev_tft_man);
    fs_register(&dev_tft_chk);
    fs_register(&dev_tft_low);
    fs_register(&dev_tft_high);
    fs_register(&dev_tft_fail);
    fs_register(&dev_tft_heater);
    fs_register(&dev_tft_apgar);
    fs_register(&dev_tft_mode);
    fs_register(&dev_tft_seek);
    fs_register(&dev_tft_text);
    fs_register(&dev_tft_clear);
}

/* One-time hardware bring-up (SPI, GPIOs, panel init sequence) --
 * called once from picoos.cpp's main(), same spot init_seg7() is
 * called from, before the /dev/tft/* devices above are ever written
 * to. Blocks for ~0.5s (the panel reset sequence) -- fine at boot,
 * same class as the existing 2s boot delay in main(). */
void tft_init(void)
{
    display_init();
    fail_blink_next = make_timeout_time_ms(FAIL_BLINK_MS);
}

/* Registered as a kernel task (task_register), not a job -- runs
 * unconditionally from boot, immune to `kill`, so the display keeps
 * animating even if bin/tftwire (the job that actually updates the
 * shadow state above) gets killed. */
void tft_flush_task(void)
{
    if (absolute_time_diff_us(get_absolute_time(), fail_blink_next) < 0) {
        fail_blink_next  = make_timeout_time_ms(FAIL_BLINK_MS);
        fail_blink_phase = !fail_blink_phase;
    }

    warmer_display_state_t state;
    memset(&state, 0, sizeof(state));

    state.mode = aut ? MODE_AUTO : MODE_MANUAL;

    // chk (safe-mode/sensor concern) blinks the SENSOR + FAIL(warning)
    // icons together -- both driven off display.c's own warning-blink
    // timer internally.
    state.sensor_connected = !chk;
    state.warning          = chk;

    // low/high/fail all read as one alarm condition to the display:
    // FAIL(warning) + ALARM icons blink together for any of the three
    // -- see dev/alarmcheck.cpp for how they combine upstream.
    bool any_fault = low || high || fail;
    state.warning |= any_fault;
    state.alarm    = any_fault;

    if      (low)  state.baby = BABY_COLD;
    else if (high) state.baby = BABY_HOT;
    else           state.baby = BABY_OK;

    if (fail) {
        state.heater_percent = 0;
        state.heater_failed  = fail_blink_phase; // true=red phase, false=grey(0%) phase
    } else {
        state.heater_percent = heater;
        state.heater_failed  = false;
    }
    state.heater_on = state.heater_percent > 0;

    // Raw forward -- display.c owns the APGAR clock entirely (tracks
    // elapsed time, derives MM:SS and the checkpoint-flash window
    // itself). It does its own rising-edge detection against its
    // last-seen apgar_start, so passing the current level here (true
    // while tftwire has it set, false once it clears) is enough to
    // (re)start the clock exactly once per button press.
    state.apgar_start = apgar;

    // Text console: screen_mode and the four text_* fields are staged
    // directly by mode/seek/text/clear's write() handlers above (no
    // derived/computed values here, same "raw forward" spirit as
    // apgar_start) -- display.c edge-detects text.seq the same way it
    // already does apgar_start.
    state.screen_mode = screen_mode;
    state.text.cmd    = text_cmd;
    state.text.seq    = text_seq;
    state.text.row    = text_row;
    strncpy(state.text.line, text_line, TEXT_COLS);
    state.text.line[TEXT_COLS] = '\0';

    display_update(&state, to_ms_since_boot(get_absolute_time()));
}
