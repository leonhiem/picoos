/**
 * prog/pid.cpp — pid <gate-device> <setpoint-device-or-literal>
 *
 * PID control, recovered from babywarmer's pid_update() (before the
 * fork ripped the whole control loop out) with two deliberate cuts:
 * no feed-forward table (this is PID alone, per Leon's "small pieces,
 * do each right" -- feed-forward was babywarmer's, not this), and no
 * state machine (that's a separate, later concern, not this program's
 * job). Same formula otherwise:
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
 * integral lives in /dev/pid/integral -- also a device, not a C
 * static, specifically so `cat /dev/pid/integral` shows live windup
 * behavior and `echo 0 > /dev/pid/integral` is a real reset you can
 * reach for while tuning gains. prev_meas stays a private static
 * here -- unlike integral it's not something a user watching or
 * resetting means anything for, it's pure derivative bookkeeping.
 *
 * Self-paced DT, same resumable-timer idea as shell.cpp's sleep/
 * run_step: a background job's poll cadence (JOB_POLL_MS, 150ms) has
 * nothing to do with the control law's own sample period, so pid only
 * performs a real step once /dev/pid/dt seconds have actually passed
 * since the last one; every other tick it just re-reads and re-echoes
 * /dev/pidout unchanged (also doubling as the "hold last output"
 * cache, so no separate C buffer is needed for that either).
 *
 * Gate-controlled, extending the same conditional-integration idea
 * the anti-windup freeze above already uses: while <gate-device>
 * doesn't read "on", pid does nothing at all -- no step, no integral
 * advance, output held exactly as-is. This is what makes "the PID
 * task should not always run" true without any job start/stop from
 * whatever orchestrates phases later -- pid runs as one permanent
 * background job and the gate decides whether it's doing anything:
 *
 *   cat /dev/skintemp | pid /dev/heaterauto /dev/setpoint > /dev/pidout &
 *   follow /dev/heaterauto /dev/pidout /dev/percent /dev/heater &
 *
 * On the tick the gate first reads "on" after being off, pid doesn't
 * step immediately -- it seeds prev_meas at the current measurement
 * and zeros the integral (bumpless start, matching babywarmer's own
 * Phase 2 entry) and arms a fresh DT-long wait before the first real
 * step. Avoids the derivative term spiking on whatever measurement
 * history built up while inactive.
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
static float prev_meas = 0.0f;

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
    if (argc < 2) return -1;
    const char *gate         = argv[0];
    const char *setpoint_arg = argv[1];

    int gfd = fs_open(gate);
    if (gfd < 0) return -1;
    char gbuf[4];
    int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
    fs_close(gfd);
    bool gate_on = (gn >= 2 && strncmp(gbuf, "on", 2) == 0);

    char mbuf[16];
    int mn = inlen < (int)sizeof(mbuf) - 1 ? inlen : (int)sizeof(mbuf) - 1;
    for (int i = 0; i < mn; i++) mbuf[i] = in[i];
    mbuf[mn] = '\0';
    float measurement = strtof(mbuf, 0);

    if (!gate_on) {
        was_active = false; // next activation reseeds bumplessly
        return echo_pidout(out, outlen);
    }

    float dt = read_value("/dev/pid/dt", 1.0f);

    if (!was_active) {
        prev_meas = measurement;
        write_dev("/dev/pid/integral", 0.0f);
        was_active = true;
        next_step = make_timeout_time_ms((int)(dt * 1000.0f));
        return echo_pidout(out, outlen);
    }

    if (absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
        return echo_pidout(out, outlen); // not due yet
    }

    float kp       = read_value("/dev/pid/kp", 3.0f);
    float ti       = read_value("/dev/pid/ti", 200.0f);
    float td       = read_value("/dev/pid/td", 5.0f);
    float setpoint = read_value(setpoint_arg, 35.0f);
    float integral = read_value("/dev/pid/integral", 0.0f);

    float error  = setpoint - measurement;
    float p_term = kp * error;
    float d_term = -kp * td * (measurement - prev_meas) / dt;
    float i_new  = integral + (kp / ti) * error * dt;
    float output = p_term + i_new + d_term;

    float out_clamped = output;
    if (out_clamped > 100.0f) out_clamped = 100.0f;
    if (out_clamped < 0.0f)   out_clamped = 0.0f;

    if (output >= 0.0f && output <= 100.0f) integral = i_new; // else: frozen, stays at previous value
    prev_meas = measurement;

    write_dev("/dev/pid/integral", integral);
    write_dev("/dev/pidout", out_clamped);

    next_step = make_timeout_time_ms((int)(dt * 1000.0f));

    return snprintf(out, outlen, "%.1f\n", out_clamped);
}

static const program_t prog_pid = {"pid", pid_run};

void pid_register(void)
{
    prog_register(&prog_pid);
}
