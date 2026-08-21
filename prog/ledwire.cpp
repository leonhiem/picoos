/**
 * prog/ledwire.cpp — ledwire: drives all 7 front-panel LEDs from
 * their condition sources, in one job
 *
 * Leon's own call: one combined job here rather than seven independent
 * small ones. Each LED's condition is a fixed, trivial device-value
 * check with no real "which pieces would I want to mix and match"
 * need the way pid/select/follow's compose-from-the-shell model exists
 * for -- these seven mappings are simply what the front panel means,
 * not something to be rewired differently on any given boot. Same call
 * prog/monitor.cpp already made for reading many devices into one
 * status line; this is the writing-side equivalent, one job driving
 * seven fixed targets instead of composing seven independent
 * pipelines for a fixed, known wiring.
 *
 * Mapping (Leon's own definitions):
 *   aut  -- /dev/heaterauto reads "on"
 *   man  -- /dev/heaterauto reads "off"
 *   warm -- /dev/state reads "boost"
 *   chk  -- /dev/state reads "safe"
 *   low  -- /dev/alarm/templow reads "on"
 *   high -- /dev/alarm/temphigh reads "on"
 *   fail -- /dev/alarm/heater reads "on"
 *
 * Writes the seven independent /dev/leds/<name> devices directly
 * (plain "on"/"off", no name-prefix formatting needed) rather than the
 * combined /dev/leds device.
 *
 * No args -- every source and target here is fixed, same as monitor
 * taking none. Runs at the normal ~150ms job cadence, un-self-paced --
 * these are plain device reads, the same cost follow/adjust/select
 * already pay every tick.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static bool dev_is(const char *path, const char *want)
{
    int fd = fs_open(path);
    if (fd < 0) return false;
    char buf[8];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return false;
    size_t wlen = strlen(want);
    return (size_t)n >= wlen && strncmp(buf, want, wlen) == 0;
}

static void set_led(const char *path, bool on)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    const char *v = on ? "on" : "off";
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int ledwire_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    bool auto_mode = dev_is("/dev/heaterauto", "on");
    set_led("/dev/leds/aut", auto_mode);
    set_led("/dev/leds/man", !auto_mode);

    set_led("/dev/leds/warm", dev_is("/dev/state", "boost"));
    set_led("/dev/leds/chk",  dev_is("/dev/state", "safe"));

    set_led("/dev/leds/low",  dev_is("/dev/alarm/templow",  "on"));
    set_led("/dev/leds/high", dev_is("/dev/alarm/temphigh", "on"));
    set_led("/dev/leds/fail", dev_is("/dev/alarm/heater",   "on"));

    return snprintf(out, outlen, "ok\n");
}

static const program_t prog_ledwire = {"ledwire", ledwire_run};

void ledwire_register(void)
{
    prog_register(&prog_ledwire);
}
