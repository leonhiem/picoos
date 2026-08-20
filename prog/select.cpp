/**
 * prog/select.cpp — select <state-device> <label>=<source> [<label>=<source> ...]
 *
 * follow's N-way sibling, not a revision of follow itself (follow's
 * own on/off two-source shape stays exactly as shipped -- this is a
 * separate, new program for a separate need: picking among more than
 * two sources by a multi-valued device's current text, not a boolean
 * gate). "Different device names/programs for different functionality,
 * not ioctl()" applies to programs here just as much as devices.
 *
 * Reads <state-device>'s current value, finds the <label>=<source>
 * pair whose label matches it, reads that pair's source (device path
 * or literal number, same convention as thresh/hyst/pid's own
 * arguments), and outputs it. No match (including any state not
 * listed, e.g. "idle") falls back to a hardcoded literal "0" -- a safe
 * default that doesn't require every caller to spell out every case.
 *
 * Meant to run as a background job, feeding a target via `>`:
 *
 *   select /dev/state boost=80 coast=0 pid=/dev/pidout safe=/dev/safepower > /dev/autopower &
 *
 * Fits in 5 args (the current JOB_MAX_ARGS), covering exactly the 4
 * real active phases -- no bump needed.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static float read_value(const char *arg)
{
    int fd = fs_open(arg);
    if (fd >= 0) {
        char buf[16];
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n >= 0) { buf[n] = '\0'; return strtof(buf, 0); }
    }
    return strtof(arg, 0);
}

static int select_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 1) return -1;

    int sfd = fs_open(argv[0]);
    if (sfd < 0) return -1;
    char sbuf[16];
    int sn = fs_read(sfd, sbuf, sizeof(sbuf) - 1);
    fs_close(sfd);
    if (sn < 0) return -1;
    sbuf[sn] = '\0';

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) continue; // malformed pair, skip rather than error the whole job
        size_t label_len = eq - argv[i];
        if ((size_t)sn >= label_len && strncmp(sbuf, argv[i], label_len) == 0) {
            float v = read_value(eq + 1);
            return snprintf(out, outlen, "%.1f\n", v);
        }
    }

    return snprintf(out, outlen, "0\n"); // no matching label -- safe default
}

static const program_t prog_select = {"select", select_run};

void select_register(void)
{
    prog_register(&prog_select);
}
