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
 * babywarmer line it replaces. state/ambient/templow/temphigh/curr-
 * fail/warn columns from that original line are gone because there's
 * no phase state machine or those specific checks here yet -- add
 * columns back if/when the devices behind them exist. prev_meas is
 * gone too -- prog/pid.cpp deliberately kept it a private static, not
 * a device (see its own header comment), so there's nothing here to
 * read it from.
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

static int monitor_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    char gbuf[4] = "off";
    int gfd = fs_open("/dev/heaterauto");
    if (gfd >= 0) {
        int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
        fs_close(gfd);
        gbuf[gn >= 0 ? gn : 0] = '\0';
    }
    bool auto_mode = strncmp(gbuf, "on", 2) == 0;

    float setp     = read_dev("/dev/setpoint");
    float skin     = read_dev("/dev/skintemp");
    float curr     = read_dev("/dev/current");
    float pidout   = read_dev("/dev/pidout");
    float percent  = read_dev("/dev/percent");
    float integral = read_dev("/dev/pid/integral");

    int pos = 0;
    if (line_idx % 10 == 0) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail,
            "mode   setp  skin   curr  pidout percent integral\n");
        if (n >= 0 && n < avail) pos += n;
    }
    line_idx++;

    int avail = outlen - pos;
    int n = snprintf(out + pos, avail,
        "%-6s %5.1f %5.1f %6.0f %6.1f %7.1f %+8.3f\n",
        auto_mode ? "auto" : "manual", setp, skin, curr, pidout, percent, integral);
    if (n >= 0 && n < avail) pos += n;

    return pos;
}

static const program_t prog_monitor = {"monitor", monitor_run};

void monitor_register(void)
{
    prog_register(&prog_monitor);
}
