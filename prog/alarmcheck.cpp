/**
 * prog/alarmcheck.cpp — alarmcheck: safety relay control + the three
 * alarm conditions, recovered from babywarmer's heater_check_task()
 * (renamed from heatercheck once temphigh/templow joined it)
 *
 * Runs as a background job (`alarmcheck &`) polled at the usual
 * JOB_POLL_MS like every other job, but self-paces to babywarmer's own
 * 15-second CHECK_INTERVAL_TIME internally -- same resumable-timer
 * pattern prog/pid.cpp and prog/phase.cpp already established, chosen
 * specifically so this doesn't need jobs.h to grow a per-job custom
 * interval (a real, documented, still-open limitation -- "all jobs
 * share one fixed poll interval"). Every other tick this is just a
 * cheap timestamp comparison, not real work.
 *
 * Per real check, reading /dev/skintemp, /dev/setpoint, /dev/heater
 * (read/write), and /dev/current:
 *
 *   1. skin > setpoint + OVERTEMP_MARGIN -> force /dev/relay off,
 *      skip everything else this check (matches the original's own
 *      early return -- an overtemp trip isn't what "heater fail" means
 *      here, and temphigh/templow are skipped this tick too since the
 *      relay's already been forced off, the more urgent action).
 *   2. heaterpower > 0 -> /dev/relay on.
 *   3. heaterpower > HIGH_POWER_THRESHOLD but current-sense is still
 *      low -> /dev/alarm/heater on (heating hard, current-sense
 *      disagrees).
 *   4. heaterpower == 0 but current-sense is still high ->
 *      /dev/alarm/heater on, and force /dev/relay off (the SSR may be
 *      stuck on, latched conducting despite being told off).
 *   5. skin > TEMPHIGH_C or skin < TEMPLOW_C -> the matching
 *      /dev/alarm/temphigh or /dev/alarm/templow.
 *
 * TEMPHIGH_C/TEMPLOW_C are Leon's own fixed testing values (40°C,
 * 10°C) -- deliberately not babywarmer's original logic, which instead
 * checked skin against setpoint+2 and ambient-2 (relative margins,
 * computed every 1s inside task_sensor, not this 15s check). Simpler
 * fixed thresholds, on purpose, for now -- easy to trigger on a bench
 * without needing a real setpoint/ambient delta. Worth revisiting
 * (relative margins, and/or a faster cadence than 15s, and/or tunable
 * devices like /dev/pid/kp) if temperature-limit responsiveness ever
 * needs to be better than this task's cadence -- not done now, Leon
 * asked for these two folded into this same task specifically.
 *
 * /dev/current already does the 8-sample averaging babywarmer's own
 * curr_sense_table rolling buffer did (see dev/current.cpp) -- nothing
 * to reimplement here, just read it live like everything else.
 *
 * Adapted from the original, not a literal copy: babywarmer's version
 * called gpio_put(PIN_HEATERSAFE, ...) directly inline. This one goes
 * through /dev/relay's existing write() instead -- one writer per GPIO
 * via the device layer, same discipline as everywhere else in this
 * namespace, rather than reaching around it.
 *
 * first_check suppresses the heater-fail bit exactly once, on this
 * job's first real check after boot -- matches the original's
 * first_time flag (avoids a false failure before there's been a real
 * reading to judge against). temphigh/templow aren't suppressed --
 * there's no equivalent warm-up concern for a plain temperature read.
 * The relay on/off logic itself is never suppressed, same as the
 * original -- only the heater-fail *bit* is.
 *
 * Ungated on purpose, unlike pid/phase -- this is a hardware safety
 * check, not part of the auto/manual control split; the original ran
 * it unconditionally in every heater mode, so this does too.
 *
 * Reacting to these three conditions (buzzer, LED, mute) is a
 * separate, later concern -- see prog/alarm.cpp, which runs at a much
 * faster cadence than this 15s check needs, on purpose.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK_INTERVAL_MS      15000
#define OVERTEMP_MARGIN         3.0f  // skin > setpoint + this -> force relay off
#define HIGH_POWER_THRESHOLD   50.0f  // heaterpower above this counts as "heating hard"
#define CURRENT_FAIL_THRESHOLD 50     // raw ADC counts, matches babywarmer's own literal "50"
#define TEMPHIGH_C             40.0f  // Leon's fixed testing threshold, "for now"
#define TEMPLOW_C              10.0f  // Leon's fixed testing threshold, "for now"

static bool have_next_step = false;
static absolute_time_t next_step;
static bool first_check = true;
static bool have_last_mode = false;
static bool last_mode = false;

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

static bool read_dev_bool(const char *path, bool fallback)
{
    int fd = fs_open(path);
    if (fd < 0) return fallback;
    char buf[8];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return fallback;
    buf[n] = '\0';
    return strncmp(buf, "on", 2) == 0;
}

static void write_dev(const char *path, const char *v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int alarmcheck_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;

    // Cheap every-tick check (like the timestamp compare below): a
    // manual<->auto flip via /dev/heaterauto can instantly change what
    // heaterpower *should* be, which can resolve (or cause) a heater-fail
    // condition -- but that condition only gets re-evaluated below on this
    // job's own 15s cadence. Without this, /dev/alarm/heater (and the
    // buzzer reading it in prog/alarm.cpp) can sit on a stale verdict for
    // up to 15s after a mode switch. Detecting the flip and forcing the
    // real check to run right now, same tick, fixes that -- have_last_mode
    // suppresses a false "changed" on the very first tick, same reason
    // first_check exists below.
    bool mode = read_dev_bool("/dev/heaterauto", last_mode);
    bool mode_changed = have_last_mode && (mode != last_mode);
    last_mode = mode;
    have_last_mode = true;

    if (!mode_changed && have_next_step && absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
        return snprintf(out, outlen, "-\n"); // not due yet
    }
    next_step = make_timeout_time_ms(CHECK_INTERVAL_MS);
    have_next_step = true;

    float skin        = read_dev("/dev/skintemp", 0.0f);
    float setpoint     = read_dev("/dev/setpoint", 35.0f);
    float heaterpower  = read_dev("/dev/heater", 0.0f);
    float curr         = read_dev("/dev/current", 0.0f);

    if (skin > (setpoint + OVERTEMP_MARGIN)) {
        write_dev("/dev/relay", "off");
        write_dev("/dev/alarm/heater", "off"); // fail starts false every check, same as the original
        first_check = false;
        return snprintf(out, outlen, "overtemp: relay off\n");
    }

    if (heaterpower > 0.0f) {
        write_dev("/dev/relay", "on");
    }

    bool fail = false;
    if (heaterpower > HIGH_POWER_THRESHOLD) {
        if (curr < CURRENT_FAIL_THRESHOLD && !first_check) fail = true;
    } else if (heaterpower == 0.0f) {
        if (curr > CURRENT_FAIL_THRESHOLD && !first_check) {
            fail = true;
            write_dev("/dev/relay", "off"); // current not zero while heater's told to be off
        }
    }
    write_dev("/dev/alarm/heater", fail ? "on" : "off");
    first_check = false;

    write_dev("/dev/alarm/temphigh", skin > TEMPHIGH_C ? "on" : "off");
    write_dev("/dev/alarm/templow",  skin < TEMPLOW_C  ? "on" : "off");

    return snprintf(out, outlen, fail ? "fail\n" : "ok\n");
}

static const program_t prog_alarmcheck = {"alarmcheck", alarmcheck_run};

void alarmcheck_register(void)
{
    prog_register(&prog_alarmcheck);
}
