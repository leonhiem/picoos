/**
 * dev/heater.cpp — /dev/heater: write-only, TPO heater power 0-100
 *
 * write /dev/heater 25   -> heater_set_power(25.0), clamped [0,100]
 * write /dev/heater on   -> heater_set_power(100)   (alias)
 * write /dev/heater off  -> heater_set_power(0)      (alias)
 *
 * on/off are just two more accepted spellings of the same 0-100 power
 * value (100 and 0), not a second mode -- still one write(), no side
 * channel -- so thresh/hyst's "on"/"off" output can drive this device
 * directly, same as it drives /dev/lamp/alarm/relay.
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
#include <cstring>

static int heater_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { heater_set_power(100.0f); return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { heater_set_power(0.0f);   return len; }

    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float pct = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number, and not on/off either

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
