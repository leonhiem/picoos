/**
 * prog/safelut.cpp — safelut: ambient-temp lookup table, safe-mode's
 * open-loop power source
 *
 * Reads ambient temp from piped input (e.g. `cat /dev/ambient |
 * safelut`), linearly interpolates babywarmer's own safe_ff_table
 * (recovered verbatim from heater.cpp's safe_feedforward()/
 * feedforward_lookup()), clamps to DUTY_SAFE_CAP (55%), outputs the
 * result. Meant to feed /dev/safepower:
 *
 *   cat /dev/ambient | safelut > /dev/safepower &
 *
 * Stateless and ungated on purpose, unlike prog/pid.cpp -- a table
 * lookup has no windup/derivative concerns, so there's nothing to
 * freeze while safe mode isn't active. Always computing is harmless,
 * same reasoning as thresh/hyst always evaluating regardless of
 * whether their output is currently the one prog/select.cpp forwards.
 *
 * The table and its cap are hardcoded here, same as hyst's heating-
 * only polarity is hardcoded -- this is specifically this warmer's
 * safe-mode curve, not a generic interpolator. Worth a tunable device
 * later if commissioning ever needs to adjust it live; not built now.
 */
#include "kernel/prog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef struct { float t_room; float pwr_pct; } lut_point_t;

static const lut_point_t safe_ff_table[] = {
    { 10.0f, 65.0f },
    { 14.0f, 58.0f },
    { 18.0f, 51.0f },
    { 20.0f, 47.0f },
    { 22.0f, 43.0f },
    { 24.0f, 39.0f },
    { 26.0f, 35.0f },
    { 28.0f, 32.0f },
    { 30.0f, 29.0f },
    { 32.0f, 25.0f },
    { 33.0f, 22.0f },
};
#define SAFE_FF_LEN (sizeof(safe_ff_table) / sizeof(safe_ff_table[0]))
#define DUTY_SAFE_CAP 55.0f

static float lookup(float t_room)
{
    if (t_room <= safe_ff_table[0].t_room) return safe_ff_table[0].pwr_pct;
    if (t_room >= safe_ff_table[SAFE_FF_LEN - 1].t_room) return safe_ff_table[SAFE_FF_LEN - 1].pwr_pct;

    for (size_t i = 0; i < SAFE_FF_LEN - 1; i++) {
        if (t_room < safe_ff_table[i + 1].t_room) {
            float span = safe_ff_table[i + 1].t_room - safe_ff_table[i].t_room;
            float frac = (t_room - safe_ff_table[i].t_room) / span;
            return safe_ff_table[i].pwr_pct
                 + frac * (safe_ff_table[i + 1].pwr_pct - safe_ff_table[i].pwr_pct);
        }
    }
    return safe_ff_table[SAFE_FF_LEN - 1].pwr_pct; // unreachable given the bounds checks above
}

static int safelut_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)argc;
    (void)argv;

    char buf[16];
    int n = inlen < (int)sizeof(buf) - 1 ? inlen : (int)sizeof(buf) - 1;
    for (int i = 0; i < n; i++) buf[i] = in[i];
    buf[n] = '\0';
    float t_room = strtof(buf, 0);

    float pwr = lookup(t_room);
    if (pwr > DUTY_SAFE_CAP) pwr = DUTY_SAFE_CAP;
    if (pwr < 0.0f)          pwr = 0.0f;

    return snprintf(out, outlen, "%.1f\n", pwr);
}

static const program_t prog_safelut = {"safelut", safelut_run};

void safelut_register(void)
{
    prog_register(&prog_safelut);
}
