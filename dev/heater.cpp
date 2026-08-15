/**
 * dev/heater.cpp — /dev/heater: write-only, TPO heater power 0-100
 *
 * write /dev/heater 25   -> heater_set_power(25.0), clamped [0,100]
 *
 * This drives a real SSR and real heat output via tpo_apply() (heater.cpp),
 * which main() registers as a task on its own schedule -- writing here
 * takes effect on tpo_apply()'s next tick, not instantly. No safety
 * interlock beyond the 0-100 clamp: deliberately raw, matching the
 * rest of this experimental line.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include <cstdlib>

static int heater_write(const char *buf, int len)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float pct = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number

    heater_set_power(pct);
    return len;
}

static const device_t dev_heater = {
    "/dev/heater",
    0,
    0,
    0,             // write-only
    heater_write,
};

void heater_register(void)
{
    fs_register(&dev_heater);
}
