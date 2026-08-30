/**
 * prog/label.cpp — label <word...> <device>: prefix a device's value
 * with literal words, e.g. `label auto = /dev/heaterauto` -> "auto = on"
 *
 * Built specifically to make text-mode TFT rows readable (see
 * dev/tft.cpp's /dev/tft/text) without needing this shell's missing
 * command substitution (no `$(...)`, no variables) -- a real shell
 * would just write `echo "auto = $(cat /dev/heaterauto)"`; this is the
 * smallest program-layer equivalent, in the same "small single-purpose
 * filter" shape as echo/cat/follow rather than a one-off TFT-specific
 * tool, so it's reusable for any future line that needs a label.
 *
 * Last argument is always the device path; everything before it is
 * printed verbatim, space-joined, immediately followed by the device's
 * value with its own trailing newline/CR stripped (so e.g. `> /dev/tft/text`
 * gets exactly one line, not two). With zero label words this degenerates
 * to a plain `cat <device>` -- harmless, not specially rejected.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static int label_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 1) return -1; // need at least the device path

    const char *device = argv[argc - 1];
    int fd = fs_open(device);
    if (fd < 0) return -1;

    char vbuf[24];
    int vn = fs_read(fd, vbuf, sizeof(vbuf) - 1);
    fs_close(fd);
    if (vn < 0) return -1;
    while (vn > 0 && (vbuf[vn - 1] == '\n' || vbuf[vn - 1] == '\r')) vn--;
    vbuf[vn] = '\0';

    // See prog/ls.cpp: snprintf()'s return value is how much it *would*
    // write, not how much it did -- must stop, not just count, once a
    // write would truncate, or pos overcounts past the real content.
    int pos = 0;
    for (int i = 0; i < argc - 1; i++) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail, "%s ", argv[i]);
        if (n < 0 || n >= avail) { pos = outlen; break; }
        pos += n;
    }
    if (pos < outlen) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail, "%s", vbuf);
        if (n >= 0 && n < avail) pos += n;
    }
    if (pos < outlen) out[pos++] = '\n';
    return pos;
}

static const program_t prog_label = {"label", label_run};

void label_register(void)
{
    prog_register(&prog_label);
}
