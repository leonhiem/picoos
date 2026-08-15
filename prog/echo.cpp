/**
 * prog/echo.cpp — echo <words...>: print its arguments, space-joined
 */
#include "kernel/prog.h"
#include <cstdio>

static int echo_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    int pos = 0;
    for (int i = 0; i < argc && pos < outlen; i++) {
        int n = snprintf(out + pos, outlen - pos, i == 0 ? "%s" : " %s", argv[i]);
        if (n < 0) break;
        pos += n;
    }
    if (pos < outlen) out[pos++] = '\n';
    return pos;
}

static const program_t prog_echo = {"echo", echo_run};

void echo_register(void)
{
    prog_register(&prog_echo);
}
