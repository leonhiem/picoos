/**
 * prog/thresh.cpp — thresh <threshold>: numeric compare, "on"/"off" out
 *
 * Reads a float from piped input, compares to a threshold, outputs
 * "on" if >= threshold else "off". The threshold argument can be
 * either a literal number ("thresh 36.5") or a device path
 * ("thresh /dev/setpoint") -- tried as a device first, read live on
 * every call; falls back to parsing it as a number if that path isn't
 * a registered device. That's what makes a live-adjustable controller
 * possible instead of a fixed one:
 *
 *   cat /dev/skintemp | thresh /dev/setpoint > /dev/lamp &
 *
 * is a bang-bang thermostat whose threshold tracks /dev/setpoint (the
 * UP/DOWN buttons, or `echo N > /dev/setpoint`) live, no restart needed.
 *
 * Note the output is "on"/"off" text, which matches /dev/lamp,
 * /dev/alarm, /dev/relay -- but not /dev/heater, which wants a 0-100
 * number. No type system enforces that; it's on you to wire compatible
 * things together, same as real Unix pipes. Turning this into an
 * actual bang-bang heater controller wants numeric on/off output plus
 * hysteresis (avoid chattering right at the threshold) -- both left
 * for later, along with the longer-term idea of feeding a real PID
 * loop instead of a simple compare.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

static int thresh_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc < 1) return -1;

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

    char tmp[16];
    int n = inlen < (int)sizeof(tmp) - 1 ? inlen : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = in[i];
    tmp[n] = '\0';
    float val = strtof(tmp, 0);

    return snprintf(out, outlen, (val >= threshold) ? "on\n" : "off\n");
}

static const program_t prog_thresh = {"thresh", thresh_run};

void thresh_register(void)
{
    prog_register(&prog_thresh);
}
