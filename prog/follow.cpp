/**
 * prog/follow.cpp — follow <gate-device> <on|off> <source-device> <target-device>
 *
 * While <gate-device> reads the given state, continuously copies
 * <source-device>'s value to <target-device>. The rest of the time it
 * holds <target-device> at "0" instead of leaving it at whatever it
 * last was -- important for something like a heater, where "not
 * actively driven" must mean "off," not "stuck at its last value."
 * Meant to run as a background job:
 *
 *   follow /dev/heaterauto off /dev/percent /dev/heater &
 *
 * makes manual mode's /dev/percent actually drive the real heater,
 * using the full 0-100 value every tick -- /dev/heater's underlying
 * tpo_apply() is a real PWM over the TPO_PERIOD_MS window, not just
 * on/off, so the full range needs to reach it, not get collapsed to a
 * binary decision the way toggle/hyst's "on"/"off" output would.
 *
 * Doesn't hardcode which gate state is "active" -- once a real auto-
 * mode control law exists, the same program drives that side too:
 *
 *   follow /dev/heaterauto on <auto-source> /dev/heater &
 *
 * Known limitation, not yet hit in practice: running *both* directions
 * on the same target at once would fight -- each tick, whichever job's
 * turn it is writes either the real value or the safety 0, so the two
 * would flicker the target between them rather than cleanly handing
 * off. Avoiding that would need each instance to know whether it was
 * active last tick, which needs real per-instance state; the stateless
 * "just check and act" shape here doesn't have anywhere to keep that
 * (see kernel/prog.h -- programs are meant to be stateless filters).
 * Not solved because it's not a real problem yet: there's only ever
 * one follow job wired up until an auto-mode source exists.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static int follow_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 4) return -1;

    const char *gate   = argv[0];
    bool want_on       = (strncmp(argv[1], "on", 2) == 0);
    const char *source = argv[2];
    const char *target = argv[3];

    int gfd = fs_open(gate);
    if (gfd < 0) return -1;
    char gbuf[4];
    int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
    fs_close(gfd);
    bool gate_on = (gn >= 2 && strncmp(gbuf, "on", 2) == 0);

    int tfd = fs_open(target);
    if (tfd < 0) return -1;

    if (gate_on != want_on) {
        fs_write(tfd, "0", 1);
        fs_close(tfd);
        return snprintf(out, outlen, "idle (%s held at 0)\n", target);
    }

    int sfd = fs_open(source);
    if (sfd < 0) { fs_close(tfd); return -1; }
    char sbuf[16];
    int sn = fs_read(sfd, sbuf, sizeof(sbuf) - 1);
    fs_close(sfd);
    if (sn < 0) { fs_close(tfd); return -1; }

    fs_write(tfd, sbuf, sn);
    fs_close(tfd);
    return snprintf(out, outlen, "%s\n", target);
}

static const program_t prog_follow = {"follow", follow_run};

void follow_register(void)
{
    prog_register(&prog_follow);
}
