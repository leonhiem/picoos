/**
 * prog/pid.cpp — pid <gate-device> <gate-value> <setpoint-device-or-literal>
 *
 * PID control, recovered from babywarmer's pid_update() (before the
 * fork ripped the whole control loop out) with two deliberate cuts:
 * no feed-forward table (this is PID alone, per Leon's "small pieces,
 * do each right" -- feed-forward was babywarmer's, not this), and no
 * state machine of its own (that's prog/phase.cpp's job, see below).
 * Same formula otherwise:
 *
 *   error   = setpoint - measurement
 *   p_term  = Kp * error
 *   d_term  = -Kp * TD * (measurement - prev_meas) / DT   (derivative
 *             on measurement, not error -- avoids a kick from a
 *             setpoint step; sign flips because dError/dt = -dMeas/dt)
 *   i_new   = integral + (Kp / TI) * error * DT
 *   output  = clamp(p_term + i_new + d_term, 0, 100)
 *   -- conditional anti-windup: integral only advances to i_new when
 *      the unclamped output wasn't saturating; otherwise it freezes.
 *
 * Kp/TI/TD/DT live in /dev/pid/{kp,ti,td,dt} (dev/pid.cpp), read live
 * every step, same "tunable while running" pattern as /dev/setpoint.
 * integral and prev_meas both live in /dev/pid/{integral,prevmeas} --
 * also devices, not C statics, specifically so `cat` shows live windup
 * behavior and, more importantly now, so prog/phase.cpp can seed them
 * itself at the moment it transitions /dev/state -- see below.
 *
 * Self-paced DT, same resumable-timer idea as shell.cpp's sleep/
 * run_step: a background job's poll cadence (JOB_POLL_MS, 150ms) has
 * nothing to do with the control law's own sample period, so pid only
 * performs a real step once /dev/pid/dt seconds have actually passed
 * since the last one; every other tick it just re-reads and re-echoes
 * /dev/pidout unchanged (also doubling as the "hold last output"
 * cache, so no separate C buffer is needed for that either).
 *
 * gate-device/gate-value: generic on purpose (revised from an earlier
 * hardcoded gate-reads-"on" version) -- <gate-device>'s reading is
 * compared against the literal <gate-value>, so this works against
 * /dev/heaterauto directly (`pid /dev/heaterauto on /dev/setpoint`,
 * the original wiring) or against /dev/state (`pid /dev/state pid
 * /dev/setpoint`, what prog/phase.cpp's richer machine actually uses).
 *
 * No seeding logic here anymore -- an earlier version reseeded
 * prev_meas/integral itself on the first tick the gate matched
 * (bumpless start). That was a workaround for not having an external
 * owner of "what should PID start from"; now prog/phase.cpp genuinely
 * is that owner, and needs *different* seed values on different
 * transitions (+3.0 integral head-start from coast, 0.0 from a safe-
 * mode recovery) that one hardcoded reseed-on-activation could never
 * express correctly anyway. pid.cpp's only remaining state is a timer
 * re-arm on the gate's inactive->active edge, so a long-idle gate
 * doesn't cause an immediately-due, stale-timed first step.
 *
 * setpoint-device-or-literal follows thresh/hyst's own convention:
 * tried as a device path first (read live), a literal number if
 * that fails.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool was_active = false;
static absolute_time_t next_step;

static float read_value(const char *arg, float fallback)
{
    int fd = fs_open(arg);
    if (fd >= 0) {
        char buf[16];
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n >= 0) { buf[n] = '\0'; return strtof(buf, 0); }
    }
    char *end;
    float v = strtof(arg, &end);
    return (end != arg) ? v : fallback;
}

static void write_dev(const char *path, float v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%.3f", v);
    fs_write(fd, buf, n);
    fs_close(fd);
}

static int echo_pidout(char *out, int outlen)
{
    int fd = fs_open("/dev/pidout");
    if (fd < 0) return -1;
    int n = fs_read(fd, out, outlen);
    fs_close(fd);
    return n < 0 ? 0 : n;
}

static int pid_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc < 3) return -1;
    const char *gate         = argv[0];
    const char *gate_value   = argv[1];
    const char *setpoint_arg = argv[2];

    int gfd = fs_open(gate);
    if (gfd < 0) return -1;
    char gbuf[16];
    int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
    fs_close(gfd);
    if (gn < 0) return -1;
    gbuf[gn] = '\0';
    size_t vlen = strlen(gate_value);
    bool gate_match = ((size_t)gn >= vlen && strncmp(gbuf, gate_value, vlen) == 0);

    if (!gate_match) {
        was_active = false; // next activation re-arms the timer fresh
        return echo_pidout(out, outlen);
    }

    float dt = read_value("/dev/pid/dt", 1.0f);

    if (!was_active) {
        was_active = true;
        next_step = make_timeout_time_ms((int)(dt * 1000.0f));
        return echo_pidout(out, outlen);
    }

    if (absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
        return echo_pidout(out, outlen); // not due yet
    }

    char mbuf[16];
    int mn = inlen < (int)sizeof(mbuf) - 1 ? inlen : (int)sizeof(mbuf) - 1;
    for (int i = 0; i < mn; i++) mbuf[i] = in[i];
    mbuf[mn] = '\0';
    float measurement = strtof(mbuf, 0);

    float kp        = read_value("/dev/pid/kp", 3.0f);
    float ti        = read_value("/dev/pid/ti", 200.0f);
    float td        = read_value("/dev/pid/td", 5.0f);
    float setpoint  = read_value(setpoint_arg, 35.0f);
    float integral  = read_value("/dev/pid/integral", 0.0f);
    float prev_meas = read_value("/dev/pid/prevmeas", measurement);

    float error  = setpoint - measurement;
    float p_term = kp * error;
    float d_term = -kp * td * (measurement - prev_meas) / dt;
    float i_new  = integral + (kp / ti) * error * dt;
    float output = p_term + i_new + d_term;

    float out_clamped = output;
    if (out_clamped > 100.0f) out_clamped = 100.0f;
    if (out_clamped < 0.0f)   out_clamped = 0.0f;

    if (output >= 0.0f && output <= 100.0f) integral = i_new; // else: frozen, stays at previous value

    write_dev("/dev/pid/integral", integral);
    write_dev("/dev/pid/prevmeas", measurement);
    write_dev("/dev/pidout", out_clamped);

    next_step = make_timeout_time_ms((int)(dt * 1000.0f));

    return snprintf(out, outlen, "%.1f\n", out_clamped);
}

static const program_t prog_pid = {"pid", pid_run};

void pid_register(void)
{
    prog_register(&prog_pid);
}
