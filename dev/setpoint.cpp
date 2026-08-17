/**
 * dev/setpoint.cpp — /dev/setpoint: read/write, the temperature setpoint
 *
 * cat /dev/setpoint          -> current value, e.g. "35.0\n"
 * echo 36.5 > /dev/setpoint  -> set directly, clamped to [SETPOINT_MIN, SETPOINT_MAX]
 *
 * No longer wired to the UP/DOWN buttons directly -- that used to be a
 * dedicated task_setpoint_buttons registered from main(), hardcoding
 * "UP/DOWN always adjusts the setpoint." Replaced by prog/adjust.cpp,
 * a generic program you run from the command line to decide what
 * UP/DOWN adjusts and under what condition, e.g.:
 *
 *   adjust /dev/heaterauto /dev/setpoint /dev/percent 0.5 &
 *
 * This device now only stores and clamps the value; it doesn't know or
 * care what's driving it.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

#define SETPOINT_MIN     30.0f
#define SETPOINT_MAX     39.0f
#define SETPOINT_DEF     35.0f

static float setpoint = SETPOINT_DEF;

static void clamp(void)
{
    if (setpoint < SETPOINT_MIN) setpoint = SETPOINT_MIN;
    if (setpoint > SETPOINT_MAX) setpoint = SETPOINT_MAX;
}

static int setpoint_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", setpoint);
}

static int setpoint_write(const char *buf, int len)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number

    setpoint = v;
    clamp();
    return len;
}

static const device_t dev_setpoint = {
    "/dev/setpoint",
    0,
    0,
    setpoint_read,
    setpoint_write,
};

void setpoint_register(void)
{
    fs_register(&dev_setpoint);
}
