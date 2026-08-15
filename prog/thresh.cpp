/**
 * prog/thresh.cpp — thresh <threshold>: numeric compare, "on"/"off" out
 *
 * Reads a float from piped input, compares to the threshold argument,
 * outputs "on" if >= threshold else "off". The actual point of this
 * one: it's a control loop built out of composed parts instead of one
 * big function, e.g.
 *
 *   cat /dev/skintemp | thresh 36.5 | write /dev/lamp &
 *
 * Note the output is "on"/"off" text, which matches /dev/lamp,
 * /dev/alarm, /dev/relay -- but not /dev/heater, which wants a 0-100
 * number. No type system enforces that; it's on you to wire compatible
 * things together, same as real Unix pipes.
 */
#include "kernel/prog.h"
#include <cstdio>
#include <cstdlib>

static int thresh_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc < 1) return -1;
    float threshold = strtof(argv[0], 0);

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
