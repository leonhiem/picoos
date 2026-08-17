/**
 * dev/percent.cpp — /dev/percent: read/write, manual heater power 0-100
 *
 * cat /dev/percent          -> current value, e.g. "10.0\n"
 * echo 25 > /dev/percent    -> set directly, clamped to [0, 100]
 *
 * The manual-mode counterpart to /dev/setpoint -- same shape, same
 * "doesn't know what's driving it" philosophy. Meant to be adjusted by
 * UP/DOWN via prog/adjust.cpp when /dev/heaterauto is off, e.g.:
 *
 *   adjust /dev/heaterauto /dev/setpoint /dev/percent 0.5 &
 *
 * Doesn't drive /dev/heater by itself -- that wiring (manual mode ->
 * /dev/percent's value actually becomes heaterpower) is a separate,
 * later step, same as /dev/heaterauto not yet driving anything either.
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

#define PERCENT_DEF 10.0f // matches the original warmer's manual-mode default

static float percent = PERCENT_DEF;

static void clamp(void)
{
    if (percent < 0.0f)   percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;
}

static int percent_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", percent);
}

static int percent_write(const char *buf, int len)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number

    percent = v;
    clamp();
    return len;
}

static const device_t dev_percent = {
    "/dev/percent",
    0,
    0,
    percent_read,
    percent_write,
};

void percent_register(void)
{
    fs_register(&dev_percent);
}
