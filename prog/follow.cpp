/**
 * prog/follow.cpp — follow <gate-device> <source-if-on> <source-if-off> <target-device>
 *
 * Continuously copies whichever source the gate currently selects to
 * the target -- on: source-if-on, off: source-if-off -- every tick,
 * unconditionally. The read-side twin of prog/adjust.cpp's write-side
 * shape (adjust picks WHICH TARGET a step goes to, based on a gate;
 * follow picks WHICH SOURCE a copy comes from):
 *
 *   adjust <gate> <target-if-on> <target-if-off> <step>
 *   follow <gate> <source-if-on>  <source-if-off>  <target>
 *
 * Always writes a live, defined value -- never an undefined/stale
 * state, and no special-cased "off means 0" baked into follow itself.
 * The caller decides what "off" means by choosing what device to read
 * in that branch (e.g. /dev/percent, which defaults to 0 but is
 * user-tunable). Meant to run as a background job, the single arbiter
 * for a shared target:
 *
 *   follow /dev/heaterauto /dev/pidout /dev/percent /dev/heater &
 *
 * -- heaterauto=on forwards prog/pid.cpp's output, off forwards manual
 * /dev/percent; either way exactly one live value reaches the heater.
 *
 * Revised from an earlier shape (follow <gate> <on|off> <source>
 * <target>, one source + "hold target at 0 when idle"). That needed
 * two mirrored instances to cover both directions of one gate -- and
 * two independent background jobs both writing the same target, each
 * on its own 150ms poll, genuinely raced: whichever job's turn came
 * up as "idle" that tick stomped 0 over whatever the other had just
 * written. Not hit while there was only ever one real source
 * (/dev/percent, manual mode); would have been hit for real the
 * moment prog/pid.cpp gave auto mode a second one. This shape has
 * exactly one writer per target, always, by construction -- no race
 * possible.
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

    const char *gate       = argv[0];
    const char *source_on  = argv[1];
    const char *source_off = argv[2];
    const char *target     = argv[3];

    int gfd = fs_open(gate);
    if (gfd < 0) return -1;
    char gbuf[4];
    int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
    fs_close(gfd);
    bool gate_on = (gn >= 2 && strncmp(gbuf, "on", 2) == 0);

    const char *source = gate_on ? source_on : source_off;

    int sfd = fs_open(source);
    if (sfd < 0) return -1;
    char sbuf[16];
    int sn = fs_read(sfd, sbuf, sizeof(sbuf) - 1);
    fs_close(sfd);
    if (sn < 0) return -1;

    int tfd = fs_open(target);
    if (tfd < 0) return -1;
    fs_write(tfd, sbuf, sn);
    fs_close(tfd);

    return snprintf(out, outlen, "%s -> %s\n", source, target);
}

static const program_t prog_follow = {"follow", follow_run};

void follow_register(void)
{
    prog_register(&prog_follow);
}
