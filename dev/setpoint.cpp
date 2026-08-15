/**
 * dev/setpoint.cpp — /dev/setpoint: read/write, the temperature setpoint
 *
 * cat /dev/setpoint          -> current value, e.g. "35.0\n"
 * echo 36.5 > /dev/setpoint  -> set directly, clamped to [SETPOINT_MIN, SETPOINT_MAX]
 *
 * Also driven by the physical UP/DOWN buttons, same as the original
 * control loop: task_setpoint_buttons (registered from main(), like
 * tpo_apply) polls button[BUTTON_UP]/button[BUTTON_DOWN] every
 * SETPOINT_POLL_MS and nudges the value by +/-SETPOINT_STEP.
 *
 * Deliberately reads button[]/buttons.cpp directly rather than going
 * through /dev/buttons: that device's read() is read-and-clear for
 * *every* pending button at once (see its own header comment), so
 * polling it here for up/down would also silently steal mute/manual/
 * start/lamp events before any other consumer -- e.g. a manual
 * `cat /dev/buttons` -- ever saw them. Touching just the two flags
 * this cares about avoids that, at the cost of one more file besides
 * buttons.cpp/dev/buttons.cpp knowing button[] exists.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

#define SETPOINT_MIN     30.0f
#define SETPOINT_MAX     39.0f
#define SETPOINT_DEF     35.0f
#define SETPOINT_STEP     0.5f
#define SETPOINT_POLL_MS   150

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

void task_setpoint_buttons(void)
{
    if (button[BUTTON_UP])   { button[BUTTON_UP]   = false; setpoint += SETPOINT_STEP; clamp(); }
    if (button[BUTTON_DOWN]) { button[BUTTON_DOWN] = false; setpoint -= SETPOINT_STEP; clamp(); }
}
