/**
 * prog/write.cpp — write <path> [text...]: write to a device
 *
 * Two forms: `write /dev/lamp on` (literal text, second argument) or
 * `... | write /dev/lamp` (piped input as the payload) -- the first
 * form is what the shell's original write builtin looked like, the
 * second is what makes write usable as the last stage of a pipeline.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstring>

static int write_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc < 1) return -1;
    int fd = fs_open(argv[0]);
    if (fd < 0) return -1;

    const char *payload = in;
    int payload_len = inlen;
    if (argc >= 2) {
        payload = argv[1];
        payload_len = (int)strlen(argv[1]);
    }

    int n = fs_write(fd, payload, payload_len);
    fs_close(fd);
    if (n < 0) return -1;

    int m = payload_len < outlen ? payload_len : outlen;
    memcpy(out, payload, m);
    return m;
}

static const program_t prog_write = {"write", write_run};

void write_register(void)
{
    prog_register(&prog_write);
}
