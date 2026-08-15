/**
 * dev/seg7.cpp — /dev/leds, /dev/seg7big, /dev/seg7small
 *
 * All three write-only. All three live in one file on purpose: the
 * 7-segment digits and front-panel LEDs are one physical 74HC595 shift
 * register chain (display.cpp's update_7seg()) -- a single call shifts
 * all 56 bits at once, so there's no way to update just the LEDs
 * without also re-sending *something* for the digits. Each device
 * updates its own slice of shared shadow state, then a full 56-bit
 * shift-out follows every write, using whatever the other two devices
 * last set.
 *
 * /dev/leds:      write "<name> on|off", one of: aut warm low high
 *                 fail chk man (led_t's 7 named bits -- the hardware
 *                 has 7 wired positions, not 6; worth checking against
 *                 the real board if that doesn't match what's expected)
 * /dev/seg7big:   write a number, e.g. "36.5" or "365" or "7" --
 *                 up to 3 digits, right-aligned, blank-padded on the
 *                 left. A literal '.' turns the dot on; because of how
 *                 the shift register is wired, the dot can only ever
 *                 appear after the middle digit (byte 6's comment in
 *                 update_7seg() calls it out) -- "1.23" and "12.3" are
 *                 physically the same display, not two different ones.
 * /dev/seg7small: same digit format, the small (setpoint-style) display.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include <cstring>

static uint8_t big_d[3]   = {SEG_BLANK, SEG_BLANK, SEG_BLANK};
static uint8_t big_dot    = 0;
static uint8_t small_d[3] = {SEG_BLANK, SEG_BLANK, SEG_BLANK};
static uint8_t small_dot  = 0;
static led_t   leds       = {0,0,0,0,0,0,0};

static void flush(void)
{
    // update_7seg()'s own header comment claims digl3/digs3 are the
    // most-significant digit -- they're not, going by the original
    // (hardware-validated) caller: digl1/digs1 is most-significant,
    // digl3/digs3 least-significant. big_d[0]/small_d[0] here are our
    // most-significant digit (parse_digits() fills them left to right),
    // so they go in the *third* argument slot, not the first.
    update_7seg(big_d[2], big_d[1], big_d[0], big_dot,
                small_d[2], small_d[1], small_d[0], small_dot, leds);
}

// Up to 3 digit characters plus an optional '.', in any order; right-
// aligns the digits into out[0..2] (out[0] = leftmost), blank-pads
// unused leading slots, and reports whether a '.' was present.
static void parse_digits(const char *buf, int len, uint8_t out[3], uint8_t *dot)
{
    char digits[3];
    int dc = 0;
    *dot = 0;
    for (int i = 0; i < len && dc < 3; i++) {
        char c = buf[i];
        if (c == '.') { *dot = 1; }
        else if (c >= '0' && c <= '9') { digits[dc++] = (uint8_t)(c - '0'); }
    }
    int pad = 3 - dc;
    for (int i = 0; i < 3; i++) {
        out[i] = (i < pad) ? SEG_BLANK : digits[i - pad];
    }
}

static int seg7big_write(const char *buf, int len)
{
    parse_digits(buf, len, big_d, &big_dot);
    flush();
    return len;
}

static int seg7small_write(const char *buf, int len)
{
    parse_digits(buf, len, small_d, &small_dot);
    flush();
    return len;
}

static int leds_write(const char *buf, int len)
{
    int sp = 0;
    while (sp < len && buf[sp] != ' ') sp++;
    if (sp == len) return -1; // no space -> no state given

    int rest_len = len - sp - 1;
    const char *rest = buf + sp + 1;
    bool on;
    if (rest_len >= 2 && strncmp(rest, "on", 2) == 0)       on = true;
    else if (rest_len >= 3 && strncmp(rest, "off", 3) == 0) on = false;
    else return -1;

    uint8_t *field;
    if      (sp == 3 && strncmp(buf, "aut",  3) == 0) field = &leds.aut;
    else if (sp == 4 && strncmp(buf, "warm", 4) == 0) field = &leds.warm;
    else if (sp == 3 && strncmp(buf, "low",  3) == 0) field = &leds.low;
    else if (sp == 4 && strncmp(buf, "high", 4) == 0) field = &leds.high;
    else if (sp == 4 && strncmp(buf, "fail", 4) == 0) field = &leds.fail;
    else if (sp == 3 && strncmp(buf, "chk",  3) == 0) field = &leds.chk;
    else if (sp == 3 && strncmp(buf, "man",  3) == 0) field = &leds.man;
    else return -1;

    *field = on ? 1 : 0;
    flush();
    return len;
}

static const device_t dev_leds      = {"/dev/leds",      0, 0, 0, leds_write};
static const device_t dev_seg7big   = {"/dev/seg7big",   0, 0, 0, seg7big_write};
static const device_t dev_seg7small = {"/dev/seg7small", 0, 0, 0, seg7small_write};

void leds_register(void)      { fs_register(&dev_leds); }
void seg7big_register(void)   { fs_register(&dev_seg7big); }
void seg7small_register(void) { fs_register(&dev_seg7small); }
