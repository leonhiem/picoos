/**
 * prog/phase.cpp — phase: the temperature-phase transition engine
 *
 * Recovered from babywarmer's task_pidctrl() (the ControlState switch
 * statement), adapted onto /dev/state instead of a C enum global.
 * States: idle -> boost -> coast -> pid, with "pid" able to drop into
 * "safe" on a watchdog trip and recover back to "pid". Owns /dev/state
 * as its only writer, plus the tick-counting bookkeeping the original
 * kept in wsafecheck_t (no_rise_ticks, coast_ticks, below_sp_ticks,
 * drop_ticks, skin_at_window_start) -- private C statics here, same as
 * prog/pid.cpp's own timer state: not meaningful to a user the way
 * /dev/pid/integral is, just internal watchdog bookkeeping.
 *
 * Self-paced ~1Hz, same resumable-timer idea as prog/pid.cpp -- the
 * thresholds below (NO_RISE_TIMEOUT_TICKS et al) all assume roughly
 * one call per second, matching babywarmer's own PID_CTRL_INTERVAL_TIME,
 * not this job's JOB_POLL_MS (150ms) poll cadence.
 *
 * idle: forced whenever /dev/heaterauto isn't "on" (manual mode, or
 * auto just switched off) -- the moment it reads "on" again, idle's
 * own tick moves straight to boost.
 *
 * boost: 80% power -- babywarmer's own STATE_PHASE1A code, despite its
 * enum comment claiming "100%"; the actual clamped value it always
 * used was 80, so that's what this recovers. The 80% literal itself
 * lives in the `select` invocation (see the recipe below), not here --
 * phase.cpp only decides *which* state is active, prog/select.cpp
 * decides what number a state means. No-rise watchdog: if skin hasn't
 * risen NO_RISE_MIN_DELTA within NO_RISE_TIMEOUT_TICKS, -> safe (NTC
 * likely not on the baby). Exits to coast once skin reaches
 * setpoint - BOOST_EXIT_MARGIN.
 *
 * coast: 0% power for COAST_TICKS seconds (lets the current-sense
 * average settle after the big power step), then -> pid, seeding
 * /dev/pid/integral at COAST_INTEGRAL_SEED -- babywarmer's own
 * documented head-start (logged steady-state integrator was landing
 * around +6.6%; seeding +3 closes most of that gap immediately without
 * overdoing initial injection, since skin is still rising at coast
 * exit and the P-term already contributes heat).
 *
 * pid: prog/pid.cpp is doing the actual control math (gated on
 * `/dev/state pid`, see the recipe). This state's own job is just the
 * two watchdogs babywarmer ran alongside it: a fast sustained drop
 * (SENSOR_DROP_RATE for SENSOR_DROP_TICKS straight, well below
 * setpoint) or a slow sustained low reading (SENSOR_SUSTAIN_TICKS) --
 * either one means the sensor probably isn't on the baby anymore, so
 * -> safe. Reads /dev/pid/prevmeas for the drop-rate comparison, the
 * same cross-reference the original made into tempctl.pid.prev_meas.
 *
 * safe: prog/safelut.cpp is producing /dev/safepower from ambient temp
 * (again, via `select`, not read here). Recovers to pid once skin
 * reaches setpoint - SAFE_RECOVER_MARGIN (a full °C looser than boost's
 * own exit margin, on purpose -- babywarmer's own hysteresis against
 * oscillating right at the watchdog trip point), seeding a plain 0.0
 * integral (not the +3 coast seed -- this is a recovery, not a cold
 * start with a known-cold thermal mass).
 *
 * Deliberately not brought back (flagged, not silently dropped):
 * babywarmer's skin_ok/amb_ok were hardcoded true unconditionally in
 * its own sensors.cpp -- vestigial, not real fault detection -- so
 * there's nothing meaningful to port. Also not here: the setpoint-jump
 * reboost path (a debounced "big setpoint step forces back to boost"),
 * and heater_check_task's current-sense/SSR-stuck-on safety relay --
 * both real, both separate concerns from this temperature-phase
 * machine, left for their own later pass.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define NO_RISE_TIMEOUT_TICKS 300   // 5 min at ~1Hz, matches babywarmer's NO_RISE_TIMEOUT_MS
#define NO_RISE_MIN_DELTA     0.3f
#define BOOST_EXIT_MARGIN     3.0f  // skin >= setpoint - this -> coast
#define COAST_TICKS           45
#define COAST_INTEGRAL_SEED   3.0f
#define SENSOR_DROP_RATE      0.08f
#define SENSOR_DROP_DELTA     2.0f
#define SENSOR_DROP_TICKS     3
#define SENSOR_SUSTAIN_MARGIN 3.0f
#define SENSOR_SUSTAIN_TICKS  20
#define SAFE_RECOVER_MARGIN   2.0f

static bool have_next_step = false;
static absolute_time_t next_step;

static int   no_rise_ticks = 0;
static float skin_at_window_start = 0.0f;
static int   coast_ticks = 0;
static int   below_sp_ticks = 0;
static int   drop_ticks = 0;

static float read_dev(const char *path, float fallback)
{
    int fd = fs_open(path);
    if (fd < 0) return fallback;
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return fallback;
    buf[n] = '\0';
    return strtof(buf, 0);
}

static bool gate_on(const char *path)
{
    int fd = fs_open(path);
    if (fd < 0) return false;
    char buf[4];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    return n >= 2 && strncmp(buf, "on", 2) == 0;
}

static bool state_is(const char *want)
{
    int fd = fs_open("/dev/state");
    if (fd < 0) return false;
    char buf[8];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    return n >= 0 && (size_t)n >= strlen(want) && strncmp(buf, want, strlen(want)) == 0;
}

static void set_state(const char *s)
{
    int fd = fs_open("/dev/state");
    if (fd < 0) return;
    fs_write(fd, s, strlen(s));
    fs_close(fd);
}

static void seed_pid(float integral_seed, float prevmeas_seed)
{
    int fd; char buf[16]; int n;

    fd = fs_open("/dev/pid/integral");
    if (fd >= 0) { n = snprintf(buf, sizeof(buf), "%.3f", integral_seed); fs_write(fd, buf, n); fs_close(fd); }

    fd = fs_open("/dev/pid/prevmeas");
    if (fd >= 0) { n = snprintf(buf, sizeof(buf), "%.3f", prevmeas_seed); fs_write(fd, buf, n); fs_close(fd); }
}

static int phase_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;

    if (have_next_step && absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
        return snprintf(out, outlen, "-\n"); // not due yet
    }
    next_step = make_timeout_time_ms(1000);
    have_next_step = true;

    bool auto_mode = gate_on("/dev/heaterauto");
    float skin      = read_dev("/dev/skintemp", 0.0f);
    float setpoint  = read_dev("/dev/setpoint", 35.0f);

    if (!auto_mode) {
        set_state("idle");
        no_rise_ticks = 0; coast_ticks = 0; below_sp_ticks = 0; drop_ticks = 0;
        return snprintf(out, outlen, "idle\n");
    }

    if (state_is("idle")) {
        no_rise_ticks = 0;
        skin_at_window_start = skin;
        set_state("boost");
        return snprintf(out, outlen, "-> boost\n");
    }

    if (state_is("boost")) {
        no_rise_ticks++;
        if (no_rise_ticks >= NO_RISE_TIMEOUT_TICKS) {
            float rise = skin - skin_at_window_start;
            if (rise < NO_RISE_MIN_DELTA) {
                set_state("safe");
                return snprintf(out, outlen, "-> safe (no rise)\n");
            }
            skin_at_window_start = skin;
            no_rise_ticks = 0;
        }
        if (skin >= (setpoint - BOOST_EXIT_MARGIN)) {
            coast_ticks = 0;
            set_state("coast");
            return snprintf(out, outlen, "-> coast\n");
        }
        return snprintf(out, outlen, "boost\n");
    }

    if (state_is("coast")) {
        coast_ticks++;
        if (coast_ticks >= COAST_TICKS) {
            seed_pid(COAST_INTEGRAL_SEED, skin);
            below_sp_ticks = 0; drop_ticks = 0;
            set_state("pid");
            return snprintf(out, outlen, "-> pid\n");
        }
        return snprintf(out, outlen, "coast\n");
    }

    if (state_is("pid")) {
        float prev_meas = read_dev("/dev/pid/prevmeas", skin);
        float drop_rate = prev_meas - skin; // positive = falling
        bool dropping_fast = drop_rate > SENSOR_DROP_RATE;
        bool far_below_sp  = skin < (setpoint - SENSOR_DROP_DELTA);

        if (dropping_fast && far_below_sp) {
            if (++drop_ticks >= SENSOR_DROP_TICKS) {
                drop_ticks = 0;
                set_state("safe");
                return snprintf(out, outlen, "-> safe (drop)\n");
            }
        } else {
            drop_ticks = 0;
        }

        if (skin < (setpoint - SENSOR_SUSTAIN_MARGIN)) {
            if (++below_sp_ticks >= SENSOR_SUSTAIN_TICKS) {
                below_sp_ticks = 0;
                set_state("safe");
                return snprintf(out, outlen, "-> safe (sustained low)\n");
            }
        } else {
            below_sp_ticks = 0;
        }

        return snprintf(out, outlen, "pid\n");
    }

    if (state_is("safe")) {
        if (skin >= (setpoint - SAFE_RECOVER_MARGIN)) {
            seed_pid(0.0f, skin);
            set_state("pid");
            return snprintf(out, outlen, "-> pid (recovered)\n");
        }
        return snprintf(out, outlen, "safe\n");
    }

    // Unrecognized /dev/state content -- shouldn't happen (its own
    // write() rejects anything outside the vocabulary), but land
    // somewhere safe rather than getting stuck doing nothing.
    set_state("idle");
    return snprintf(out, outlen, "-> idle (unknown state)\n");
}

static const program_t prog_phase = {"phase", phase_run};

void phase_register(void)
{
    prog_register(&prog_phase);
}
