/**
 * prog/toggle.cpp — toggle <button-device> <target-device>
 *
 * If the named button was pressed since the last check, read the
 * target device's current on/off state and write back the opposite.
 * Otherwise does nothing this call ("idle").
 *
 * Meant to run as a background job, not once in the foreground:
 *
 *   toggle /dev/buttons/lamp /dev/lamp &
 *
 * makes the physical LAMP button actually toggle /dev/lamp, forever.
 * No blocking/waiting primitive needed for this -- the job system's
 * own periodic re-invocation (kernel/task.h, via jobs.cpp) already IS
 * "wait without wasting CPU," the same way task_setpoint_buttons
 * already polls UP/DOWN. See jobs.h for why background jobs don't
 * print their per-tick "idle" result to the terminal.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static int toggle_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 2) return -1;

    int bfd = fs_open(argv[0]);
    if (bfd < 0) return -1;
    char bbuf[4];
    int bn = fs_read(bfd, bbuf, sizeof(bbuf) - 1);
    fs_close(bfd);
    bool pressed = (bn > 0 && bbuf[0] == '1');

    if (!pressed) {
        return snprintf(out, outlen, "idle\n");
    }

    int dfd = fs_open(argv[1]);
    if (dfd < 0) return -1;
    char dbuf[8];
    int dn = fs_read(dfd, dbuf, sizeof(dbuf) - 1);
    if (dn < 0) { fs_close(dfd); return -1; }
    bool on = (dn >= 2 && strncmp(dbuf, "on", 2) == 0);

    const char *next = on ? "off" : "on";
    fs_write(dfd, next, on ? 3 : 2);
    fs_close(dfd);

    return snprintf(out, outlen, "%s\n", next);
}

static const program_t prog_toggle = {"toggle", toggle_run};

void toggle_register(void)
{
    prog_register(&prog_toggle);
}
