#include "prog.h"
#include <cstring>

static const program_t *progs[PROG_MAX];
static int count = 0;

void prog_register(const program_t *p)
{
    if (count >= PROG_MAX) return; // TODO: report overflow once we have a console device
    progs[count++] = p;
}

const program_t *prog_find(const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(progs[i]->name, name) == 0) return progs[i];
    }
    return 0;
}

int prog_count(void)
{
    return count;
}

const char *prog_name(int index)
{
    if (index < 0 || index >= count) return 0;
    return progs[index]->name;
}
