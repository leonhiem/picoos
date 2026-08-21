/**
 * dev/heaterfail.cpp — /dev/heaterfail: read/write, current-sense fail bit
 *
 * "on" = prog/heatercheck.cpp detected a current-sense mismatch against
 * what /dev/heater commanded -- either heating hard but current-sense
 * says otherwise (SSR possibly not conducting), or supposedly off but
 * current-sense says otherwise (SSR possibly stuck on). "off" = last
 * check was fine. Written by heatercheck roughly every 15s; read/write
 * like every other device, nothing stops a human writing it directly,
 * harmless (heatercheck will just overwrite it on its own next check).
 *
 * Not wired to anything yet -- tying this to the front-panel LEDs and
 * the alarm is a later, separate step (Leon's own "one step at a
 * time" -- this device's whole job right now is just to exist and be
 * readable).
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static bool fail = false;

static int heaterfail_read(char *buf, int len)
{
    return snprintf(buf, len, fail ? "on\n" : "off\n");
}

static int heaterfail_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { fail = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { fail = false; return len; }
    return -1;
}

static const device_t dev_heaterfail = {
    "/dev/heaterfail",
    0,
    0,
    heaterfail_read,
    heaterfail_write,
};

void heaterfail_register(void)
{
    fs_register(&dev_heaterfail);
}
