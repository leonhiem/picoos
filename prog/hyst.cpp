/**
 * prog/hyst.cpp — hyst <threshold> <band>: heating bang-bang w/ hysteresis
 *
 * Reads a float from piped input (e.g. skin temp), compares it against
 * a threshold with a <band>-degree dead zone centered on it, outputs
 * "on"/"off". Heating polarity, on purpose -- this exists specifically
 * to drive a heater, unlike thresh's generic (and opposite-polarity)
 * "on when >= threshold":
 *
 *   on:  val <= threshold - band/2
 *   off: val >= threshold + band/2
 *   (otherwise: stays whatever it last was)
 *
 * threshold, like thresh's argument, can be a literal number ("36.5")
 * or a device path ("/dev/setpoint", read live every call) -- tried as
 * a device first, falls back to a literal number.
 *
 *   cat /dev/skintemp | hyst /dev/setpoint 1.0 > /dev/lamp &
 *
 * is a real thermostat: heat calls for "on" until skin temp reaches
 * setpoint+0.5, then stays "off" until it drops back to setpoint-0.5 --
 * no chattering right at the setpoint the way a plain thresh would.
 *
 * Unlike every other program so far, this one has to keep state
 * (whether it's currently calling for heat) between calls -- that's
 * the whole point of hysteresis, so it's a deliberate, documented
 * exception to kernel/prog.h's "programs are stateless filters" rule,
 * not an oversight. The state is one global, shared by every pipeline
 * that happens to use hyst -- fine for one thermostat; running two
 * independent hyst-based background jobs at once would have them
 * stomp on each other's on/off memory. Not solved; worth knowing.
 *
 * Output is still "on"/"off" text, so it fits /dev/lamp/alarm/relay,
 * not /dev/heater's 0-100 number -- same note as thresh.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

static bool heating = false;

static int hyst_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc < 2) return -1;

    float threshold;
    int fd = fs_open(argv[0]);
    if (fd >= 0) {
        char tbuf[16];
        int n = fs_read(fd, tbuf, sizeof(tbuf) - 1);
        fs_close(fd);
        if (n < 0) return -1;
        tbuf[n] = '\0';
        threshold = strtof(tbuf, 0);
    } else {
        threshold = strtof(argv[0], 0);
    }

    float band = strtof(argv[1], 0);
    if (band < 0.0f) band = -band;

    char tmp[16];
    int n = inlen < (int)sizeof(tmp) - 1 ? inlen : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = in[i];
    tmp[n] = '\0';
    float val = strtof(tmp, 0);

    float lo = threshold - band / 2.0f;
    float hi = threshold + band / 2.0f;

    if (heating) {
        if (val >= hi) heating = false;
    } else {
        if (val <= lo) heating = true;
    }

    return snprintf(out, outlen, heating ? "on\n" : "off\n");
}

static const program_t prog_hyst = {"hyst", hyst_run};

void hyst_register(void)
{
    prog_register(&prog_hyst);
}
