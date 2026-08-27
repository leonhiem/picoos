/**
 * prog/tftwire.cpp — tftwire: drives /dev/tft/* from the same condition
 * sources bin/ledwire already drives into /dev/leds/*, in one job
 *
 * The ST7735 display is a second, independent consumer of the same
 * underlying signals ledwire already wires up -- "a great addition", a
 * nicer front panel running alongside the LEDs, not a replacement for
 * them (see tft-picoos-integration-spec.md). Same call as ledwire's own
 * header comment: one combined job here rather than seven independent
 * ones, since every source and target is fixed, not something to mix
 * and match across boots.
 *
 * Mapping:
 *   aut  -- /dev/heaterauto reads "on"
 *   man  -- /dev/heaterauto reads "off"
 *   chk  -- /dev/state reads "safe"
 *   low  -- /dev/alarm/templow reads "on"
 *   high -- /dev/alarm/temphigh reads "on"
 *   fail -- /dev/alarm/heater reads "on"
 *   heater -- /dev/heater's numeric value, passed through as-is
 *   apgar  -- /dev/buttons/start's read-and-clear "1"/"0", forwarded
 *             as-is (added 2026-08-28, for the display's new APGAR
 *             clock -- see dev/tft.cpp). Nothing computed here: the
 *             button read already gives a one-shot pulse (auto-clears
 *             after being seen), so one job tick sees "1" -> writes
 *             /dev/tft/apgar on, the next tick reads "0" again ->
 *             writes off. display.c does its own rising-edge detection
 *             on that on/off transition.
 *
 * (ledwire's `warm` has no /dev/tft equivalent -- redundant with
 * heater's own color on the display, confirmed in the integration spec.)
 *
 * No args -- every source and target here is fixed, same as ledwire and
 * monitor taking none. Runs at the normal ~150ms job cadence,
 * un-self-paced -- plain device reads, same cost every other wiring job
 * already pays every tick. The display's own redraw/animation rate is
 * unrelated to this cadence -- see dev/tft.cpp's tft_flush_task, a
 * kernel task, not a job.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

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

static void set_tft(const char *path, bool on)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    const char *v = on ? "on" : "off";
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int tftwire_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    bool auto_mode = dev_is("/dev/heaterauto", "on");
    set_tft("/dev/tft/aut", auto_mode);
    set_tft("/dev/tft/man", !auto_mode);

    set_tft("/dev/tft/chk", dev_is("/dev/state", "safe"));

    set_tft("/dev/tft/low",  dev_is("/dev/alarm/templow",  "on"));
    set_tft("/dev/tft/high", dev_is("/dev/alarm/temphigh", "on"));
    set_tft("/dev/tft/fail", dev_is("/dev/alarm/heater",   "on"));

    int fd = fs_open("/dev/heater");
    if (fd >= 0) {
        char buf[8];
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n > 0) {
            buf[n] = '\0';
            int fd2 = fs_open("/dev/tft/heater");
            if (fd2 >= 0) {
                char num[8];
                int nlen = snprintf(num, sizeof(num), "%d", atoi(buf));
                fs_write(fd2, num, nlen);
                fs_close(fd2);
            }
        }
    }

    set_tft("/dev/tft/apgar", dev_is("/dev/buttons/start", "1"));

    return snprintf(out, outlen, "ok\n");
}

static const program_t prog_tftwire = {"tftwire", tftwire_run};

void tftwire_register(void)
{
    prog_register(&prog_tftwire);
}
