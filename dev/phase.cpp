/**
 * dev/phase.cpp — /dev/state, /dev/autopower, /dev/safepower
 *
 * The phase state machine's own devices, bundled in one file the same
 * reason dev/pid.cpp bundles its own tunables -- one logical unit,
 * even though each is independently addressable.
 *
 * /dev/state: read/write, one of idle|boost|coast|pid|safe. Written
 * only by prog/phase.cpp (its transition engine); just stores and
 * clamps to that vocabulary here, same "doesn't know what's driving
 * it" shape as every other device -- writing an unrecognized word is
 * rejected rather than silently accepted.
 *
 * /dev/autopower: whatever the currently-active phase says heater
 * power should be. Written by prog/select.cpp, read by follow as the
 * auto-mode source in the unchanged top-level wiring:
 *
 *   follow /dev/heaterauto /dev/autopower /dev/percent /dev/heater &
 *
 * /dev/safepower: safe mode's lookup-table output, written by
 * prog/safelut.cpp, read by prog/select.cpp when /dev/state is safe.
 * Kept separate from /dev/pidout -- safe mode isn't PID, it's a
 * different control law with a different owner.
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const char *STATE_NAMES[] = { "idle", "boost", "coast", "pid", "safe" };
#define STATE_COUNT (int)(sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]))

static char state[8] = "idle";
static float autopower = 0.0f;
static float safepower = 0.0f;

static int state_read(char *buf, int len)
{
    return snprintf(buf, len, "%s\n", state);
}

static int state_write(const char *buf, int len)
{
    for (int i = 0; i < STATE_COUNT; i++) {
        size_t n = strlen(STATE_NAMES[i]);
        if ((size_t)len >= n && strncmp(buf, STATE_NAMES[i], n) == 0) {
            strcpy(state, STATE_NAMES[i]);
            return len;
        }
    }
    return -1; // not one of idle/boost/coast/pid/safe
}

static int write_float(const char *buf, int len, float *dst)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1;
    if (v < 0.0f)   v = 0.0f;
    if (v > 100.0f) v = 100.0f;
    *dst = v;
    return len;
}

static int autopower_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", autopower); }
static int autopower_write(const char *buf, int len) { return write_float(buf, len, &autopower); }

static int safepower_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", safepower); }
static int safepower_write(const char *buf, int len) { return write_float(buf, len, &safepower); }

static const device_t dev_state      = { "/dev/state",      0, 0, state_read,      state_write };
static const device_t dev_autopower  = { "/dev/autopower",  0, 0, autopower_read,  autopower_write };
static const device_t dev_safepower  = { "/dev/safepower",  0, 0, safepower_read,  safepower_write };

void phase_devices_register(void)
{
    fs_register(&dev_state);
    fs_register(&dev_autopower);
    fs_register(&dev_safepower);
}
