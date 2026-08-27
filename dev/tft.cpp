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

#define FAIL_BLINK_MS 500
static bool            fail_blink_phase = false;
static absolute_time_t fail_blink_next;

/* ═══════════════════════════════════════════════════
   /dev/tft/<name> -- eight independent read/write devices, one per
   shadow field, same idea (and same hand-duplication tradeoff, fine at
   eight) as dev/seg7.cpp's /dev/leds/<name>.
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

static const device_t dev_tft_aut    = {"/dev/tft/aut",    0, 0, aut_read,    aut_write};
static const device_t dev_tft_man    = {"/dev/tft/man",    0, 0, man_read,    man_write};
static const device_t dev_tft_chk    = {"/dev/tft/chk",    0, 0, chk_read,    chk_write};
static const device_t dev_tft_low    = {"/dev/tft/low",    0, 0, low_read,    low_write};
static const device_t dev_tft_high   = {"/dev/tft/high",   0, 0, high_read,   high_write};
static const device_t dev_tft_fail   = {"/dev/tft/fail",   0, 0, fail_read,   fail_write};
static const device_t dev_tft_heater = {"/dev/tft/heater", 0, 0, heater_read, heater_write};
static const device_t dev_tft_apgar  = {"/dev/tft/apgar",  0, 0, apgar_read,  apgar_write};

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

    display_update(&state, to_ms_since_boot(get_absolute_time()));
}
