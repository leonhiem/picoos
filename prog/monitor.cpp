/**
 * prog/monitor.cpp — monitor: one status line combining several
 * /dev/* readings, with a repeated column header every 10 lines
 *
 * A direct descendant of babywarmer's own hardcoded debug printf --
 * same idea (one glance at everything that matters), rebuilt as one
 * more program reading the same devices anyone else can `cat`,
 * instead of a printf wired straight to internal C structs. Meant to
 * run under the `watch` shell builtin (see shell.cpp), not
 * backgrounded with `&` -- a monitor is something you watch and then
 * stop watching, not a job left running unattended:
 *
 *   watch 1000 monitor
 *
 * Deliberately not a generic "print N devices" program -- like hyst's
 * heating-only polarity, this is specifically a warmer status line,
 * with its own fixed columns and a repeated header, same shape as the
 * babywarmer line it replaces. templow/temphigh/curr-fail/warn columns
 * from that original line are still gone -- there's no equivalent
 * device for any of them yet. phase/ambient/heat came back once
 * /dev/state, /dev/ambient, and a readable /dev/heater existed to
 * read them from. prev_meas stays gone -- it's still prog/pid.cpp's
 * private static, not a device (see its own header comment).
 *
 * Column header repeats every 10 lines (matches the original's
 * line_idx % 10), tracked as one more static counter -- same
 * documented "programs are supposed to be stateless filters, this one
 * deliberately isn't" exception as hyst's/pid's own state.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int line_idx = 0;

static float read_dev(const char *path)
{
    int fd = fs_open(path);
    if (fd < 0) return 0.0f;
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return 0.0f;
    buf[n] = '\0';
    return strtof(buf, 0);
}

// For short text devices (/dev/heaterauto, /dev/state) -- copies at
// most outlen-1 bytes into out, trimming any trailing newline, and
// always nul-terminates. Falls back to "?" if the device is missing.
static void read_dev_str(const char *path, char *out, int outlen)
{
    int fd = fs_open(path);
    if (fd < 0) { snprintf(out, outlen, "?"); return; }
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) { snprintf(out, outlen, "?"); return; }
    if (n > 0 && buf[n - 1] == '\n') n--;
    int m = n < outlen - 1 ? n : outlen - 1;
    for (int i = 0; i < m; i++) out[i] = buf[i];
    out[m] = '\0';
}

static int monitor_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    char mode[8];
    read_dev_str("/dev/heaterauto", mode, sizeof(mode));
    bool auto_mode = strncmp(mode, "on", 2) == 0;

    char phase[8];
    read_dev_str("/dev/state", phase, sizeof(phase));

    float setp     = read_dev("/dev/setpoint");
    float skin     = read_dev("/dev/skintemp");
    float amb      = read_dev("/dev/ambient");
    float heat     = read_dev("/dev/heater");
    float curr     = read_dev("/dev/current");
    float pidout   = read_dev("/dev/pidout");
    float percent  = read_dev("/dev/percent");
    float integral = read_dev("/dev/pid/integral");

    int pos = 0;
    if (line_idx % 10 == 0) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail,
            "mode   phase setp  skin  amb   heat  curr  pidout percent integral\n");
        if (n >= 0 && n < avail) pos += n;
    }
    line_idx++;

    int avail = outlen - pos;
    int n = snprintf(out + pos, avail,
        "%-6s %-5s %5.1f %5.1f %5.1f %5.1f %6.0f %6.1f %7.1f %+8.3f\n",
        auto_mode ? "auto" : "manual", phase, setp, skin, amb, heat, curr, pidout, percent, integral);
    if (n >= 0 && n < avail) pos += n;

    return pos;
}

static const program_t prog_monitor = {"monitor", monitor_run};

void monitor_register(void)
{
    prog_register(&prog_monitor);
}
