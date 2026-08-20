/**
 * dev/pid.cpp — /dev/pid/{kp,ti,td,dt,integral} and /dev/pidout
 *
 * The tunable parameters for prog/pid.cpp's control law, plus
 * /dev/pidout -- the device pid writes its computed output to (kept
 * separate from /dev/heater itself; see prog/follow.cpp for how
 * /dev/heaterauto picks whether pidout or /dev/percent actually
 * reaches the heater). Bundled in one file for the same reason
 * dev/seg7.cpp bundles /dev/leds with both seg7 displays: these are
 * one logical unit (one control loop's tunables), even though each is
 * its own device -- "different device names for different
 * functionality, not ioctl()."
 *
 * kp/ti/td/dt: read/write, same "just stores and clamps, doesn't know
 * what's driving it" shape as /dev/setpoint. Defaults are babywarmer's
 * hardware-tuned values (Kp=3.0 %/°C, TI=200s, TD=5s, DT=1s) -- see
 * prog/pid.cpp for the formula these feed. ti/dt are floored above
 * zero (not just clamped to it) because both land in a divisor in
 * that formula -- a literal 0 would blow up, not just misbehave.
 *
 * integral: read/write, exposes prog/pid.cpp's live integral term.
 * Useful for watching windup while tuning gains on real hardware, and
 * `echo 0 > /dev/pid/integral` is a genuine, useful "reset windup"
 * operation during tuning -- not a debug backdoor, just this state
 * living in the same namespace as everything else, same as any other
 * device.
 *
 * prevmeas: read/write, derivative-on-measurement's last-measurement
 * tracking. Promoted from a private C static in prog/pid.cpp to a real
 * device specifically because prog/phase.cpp's transition engine needs
 * to seed it (bumpless start into PID mode) at exactly the moment it
 * changes /dev/state -- state only pid.cpp itself used to touch, now
 * genuinely shared between the two, so it has to be a device, not a
 * hidden static one file can't reach from another.
 *
 * pidout: read/write, holds the PID's last computed output (0-100).
 * Written by prog/pid.cpp exactly like any other device write (plain
 * fs_write) -- nothing stops a human from writing it directly too,
 * which is harmless (follow only reads it once a second at most).
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

#define KP_DEF   3.0f
#define TI_DEF 200.0f
#define TD_DEF   5.0f
#define DT_DEF   1.0f

#define TI_MIN   1.0f   // floor, not just a clamp -- Kp/TI divides by this
#define DT_MIN   0.1f   // floor, not just a clamp -- .../DT divides by this

static float kp = KP_DEF;
static float ti = TI_DEF;
static float td = TD_DEF;
static float dt = DT_DEF;
static float integral = 0.0f;
static float prevmeas = 0.0f;
static float pidout = 0.0f;

static int write_float(const char *buf, int len, float *dst, float lo, float hi)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number

    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *dst = v;
    return len;
}

static int kp_read(char *buf, int len)        { return snprintf(buf, len, "%.2f\n", kp); }
static int kp_write(const char *buf, int len) { return write_float(buf, len, &kp, 0.0f, 100.0f); }

static int ti_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", ti); }
static int ti_write(const char *buf, int len) { return write_float(buf, len, &ti, TI_MIN, 10000.0f); }

static int td_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", td); }
static int td_write(const char *buf, int len) { return write_float(buf, len, &td, 0.0f, 1000.0f); }

static int dt_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", dt); }
static int dt_write(const char *buf, int len) { return write_float(buf, len, &dt, DT_MIN, 60.0f); }

static int integral_read(char *buf, int len)        { return snprintf(buf, len, "%.3f\n", integral); }
static int integral_write(const char *buf, int len) { return write_float(buf, len, &integral, -1000.0f, 1000.0f); }

static int prevmeas_read(char *buf, int len)        { return snprintf(buf, len, "%.2f\n", prevmeas); }
static int prevmeas_write(const char *buf, int len) { return write_float(buf, len, &prevmeas, -1000.0f, 1000.0f); }

static int pidout_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", pidout); }
static int pidout_write(const char *buf, int len) { return write_float(buf, len, &pidout, 0.0f, 100.0f); }

static const device_t dev_kp       = { "/dev/pid/kp",       0, 0, kp_read,       kp_write };
static const device_t dev_ti       = { "/dev/pid/ti",       0, 0, ti_read,       ti_write };
static const device_t dev_td       = { "/dev/pid/td",       0, 0, td_read,       td_write };
static const device_t dev_dt       = { "/dev/pid/dt",       0, 0, dt_read,       dt_write };
static const device_t dev_integral = { "/dev/pid/integral", 0, 0, integral_read, integral_write };
static const device_t dev_prevmeas = { "/dev/pid/prevmeas", 0, 0, prevmeas_read, prevmeas_write };
static const device_t dev_pidout   = { "/dev/pidout",       0, 0, pidout_read,   pidout_write };

void pid_devices_register(void)
{
    fs_register(&dev_kp);
    fs_register(&dev_ti);
    fs_register(&dev_td);
    fs_register(&dev_dt);
    fs_register(&dev_integral);
    fs_register(&dev_prevmeas);
    fs_register(&dev_pidout);
}
