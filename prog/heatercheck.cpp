/**
 * prog/heatercheck.cpp — heatercheck: safety relay control + current-
 * sense fail detection, recovered from babywarmer's heater_check_task()
 *
 * Runs as a background job (`heatercheck &`) polled at the usual
 * JOB_POLL_MS like every other job, but self-paces to babywarmer's own
 * 15-second CHECK_INTERVAL_TIME internally -- same resumable-timer
 * pattern prog/pid.cpp and prog/phase.cpp already established, chosen
 * specifically so this doesn't need jobs.h to grow a per-job custom
 * interval (a real, documented, still-open limitation -- "all jobs
 * share one fixed poll interval"). Every other tick this is just a
 * cheap timestamp comparison, not real work.
 *
 * Per real check, reading /dev/skintemp, /dev/setpoint, /dev/heater
 * (now read/write), and /dev/current:
 *
 *   1. skin > setpoint + OVERTEMP_MARGIN -> force /dev/relay off,
 *      skip everything else this check (matches the original's own
 *      early return -- an overtemp trip isn't what "fail" means here).
 *   2. heaterpower > 0 -> /dev/relay on.
 *   3. heaterpower > HIGH_POWER_THRESHOLD but current-sense is still
 *      low -> fail (heating hard, current-sense disagrees).
 *   4. heaterpower == 0 but current-sense is still high -> fail, and
 *      force /dev/relay off (the SSR may be stuck on, latched
 *      conducting despite being told off).
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
 * first_check suppresses the fail bit exactly once, on this job's
 * first real check after boot -- matches the original's `first_time`
 * flag (avoids a false failure before there's been a real reading to
 * judge against). The relay on/off logic itself is never suppressed,
 * same as the original -- only the fail *bit* is.
 *
 * Ungated on purpose, unlike pid/phase -- this is a hardware safety
 * check, not part of the auto/manual control split; the original ran
 * it unconditionally in every heater mode, so this does too.
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

static bool have_next_step = false;
static absolute_time_t next_step;
static bool first_check = true;

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

static void write_dev(const char *path, const char *v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int heatercheck_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;

    if (have_next_step && absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
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
        write_dev("/dev/heaterfail", "off"); // fail starts false every check, same as the original
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

    write_dev("/dev/heaterfail", fail ? "on" : "off");
    first_check = false;

    return snprintf(out, outlen, fail ? "fail\n" : "ok\n");
}

static const program_t prog_heatercheck = {"heatercheck", heatercheck_run};

void heatercheck_register(void)
{
    prog_register(&prog_heatercheck);
}
