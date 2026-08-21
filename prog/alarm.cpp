/**
 * prog/alarm.cpp — alarm <cond-device> [<cond-device> ...]: combine
 * alarm conditions, drive the buzzer/LED, honor the mute button
 *
 * Recovered from babywarmer's task_alarm(), split into its own program
 * running at the shell's normal ~150ms job cadence -- deliberately
 * NOT self-paced like alarmcheck's 15s. A mute button press needs to
 * register close to instantly (babywarmer ran task_alarm at 160ms for
 * exactly this reason), while alarmcheck's conditions change slowly
 * (temperature, current-sense). Same "small pieces, do them right"
 * reasoning that gave each program its own appropriate cadence instead
 * of forcing one interval on both.
 *
 * Reads every <cond-device> given and ORs them: any reading "on" means
 * the alarm condition is active. Meant to run as:
 *
 *   alarm /dev/alarm/heater /dev/alarm/temphigh /dev/alarm/templow &
 *
 * While active: lights the alarm LED unconditionally (mute silences
 * the *buzzer* only, never the LED -- always a visible indication,
 * even snoozed, same as the original) and sounds the buzzer unless
 * currently muted. Reads button[BUTTON_MUTE] directly and clears it,
 * same as adjust.cpp/toggle.cpp already do for their own buttons --
 * not through /dev/buttons/mute, whose read() returns "1"/"0" (see
 * dev/buttons.cpp), a different vocabulary than the "on"/"off" this
 * program reads from its condition devices; going straight to the raw
 * flag sidesteps that mismatch the same way adjust/toggle already
 * sidestep /dev/buttons's own all-or-nothing aggregate read. Arms a
 * MUTE_MS snooze whenever pressed, regardless of whether the alarm is
 * active yet -- matches the original, which let a mute press pre-arm
 * before a condition even trips. Mute auto-clears once the timer
 * elapses, and is force-cleared the moment the condition itself clears
 * -- a mute doesn't carry over to a future, unrelated alarm event.
 *
 * Drives /dev/alarm's three-state write() (see dev/alarm.cpp): "on"
 * (LED+buzzer), "muted" (LED only), "off" (neither).
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "warmer.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

#define MUTE_MS 60000 // matches babywarmer's own MUTE_INTERVAL_TIME (1 min)

static bool muted = false;
static bool have_muted_time = false;
static absolute_time_t muted_time;

static bool read_on(const char *path)
{
    int fd = fs_open(path);
    if (fd < 0) return false;
    char buf[4];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    return n >= 2 && strncmp(buf, "on", 2) == 0;
}

static void write_alarm(const char *v)
{
    int fd = fs_open("/dev/alarm");
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int alarmctl_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 1) return -1;

    bool active = false;
    for (int i = 0; i < argc; i++) {
        if (read_on(argv[i])) { active = true; break; }
    }

    // Mute button: read-and-clear the raw flag, arms regardless of
    // `active` -- matches the original, a pre-arm before a condition
    // trips is harmless.
    if (button[BUTTON_MUTE]) {
        button[BUTTON_MUTE] = false;
        muted = true;
        muted_time = make_timeout_time_ms(MUTE_MS);
        have_muted_time = true;
    }

    if (!active) {
        muted = false;
        write_alarm("off");
        return snprintf(out, outlen, "off\n");
    }

    if (muted && have_muted_time && absolute_time_diff_us(get_absolute_time(), muted_time) <= 0) {
        muted = false; // snooze elapsed
    }

    write_alarm(muted ? "muted" : "on");
    return snprintf(out, outlen, muted ? "muted\n" : "on\n");
}

static const program_t prog_alarmctl = {"alarm", alarmctl_run};

void alarmctl_register(void)
{
    prog_register(&prog_alarmctl);
}
