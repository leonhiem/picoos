/**
 * prog/adjust.cpp — adjust <gate-device> <target-if-on> <target-if-off> <step>
 *
 * Reads UP/DOWN; if either was pressed since the last check, reads
 * <gate-device>'s on/off state to pick a target (on -> target-if-on,
 * off -> target-if-off), reads that target's current numeric value,
 * adds or subtracts <step>, writes the result back. Clamping is left
 * entirely to the target device's own write() (/dev/setpoint and
 * /dev/percent both already clamp themselves) -- adjust doesn't know
 * or care what range is valid for whatever it's pointed at.
 *
 * This is what "disconnects" UP/DOWN from any one target: which
 * device gets adjusted, and under what condition, is now a pipeline
 * you choose to run from the command line, not a fact hardcoded into
 * a device file. Meant to run as a background job:
 *
 *   adjust /dev/heaterauto /dev/setpoint /dev/percent 0.5 &
 *
 * Reads button[BUTTON_UP]/button[BUTTON_DOWN] directly rather than
 * through /dev/buttons, same reasoning as the old task_setpoint_buttons
 * this replaces: /dev/buttons's read-and-clear is all-or-nothing and
 * would steal every other pending button's event too.
 *
 * If both UP and DOWN happen to be pressed at once, UP wins this tick
 * and DOWN's flag is left set for the next one, rather than either
 * cancelling out or getting silently dropped.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "warmer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int adjust_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 4) return -1;

    bool up = button[BUTTON_UP];
    bool down = button[BUTTON_DOWN];
    if (!up && !down) {
        return snprintf(out, outlen, "idle\n");
    }
    float direction;
    if (up) { button[BUTTON_UP] = false;   direction = 1.0f; }
    else    { button[BUTTON_DOWN] = false; direction = -1.0f; }

    int gfd = fs_open(argv[0]);
    if (gfd < 0) return -1;
    char gbuf[4];
    int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
    fs_close(gfd);
    bool gate_on = (gn >= 2 && strncmp(gbuf, "on", 2) == 0);
    const char *target = gate_on ? argv[1] : argv[2];

    float step = strtof(argv[3], 0);

    int tfd = fs_open(target);
    if (tfd < 0) return -1;
    char tbuf[16];
    int tn = fs_read(tfd, tbuf, sizeof(tbuf) - 1);
    if (tn < 0) { fs_close(tfd); return -1; }
    tbuf[tn] = '\0';
    float val = strtof(tbuf, 0) + direction * step;

    char wbuf[16];
    int wn = snprintf(wbuf, sizeof(wbuf), "%.2f", val);
    fs_write(tfd, wbuf, wn);
    fs_close(tfd);

    return snprintf(out, outlen, "%s -> %.2f\n", target, val);
}

static const program_t prog_adjust = {"adjust", adjust_run};

void adjust_register(void)
{
    prog_register(&prog_adjust);
}
