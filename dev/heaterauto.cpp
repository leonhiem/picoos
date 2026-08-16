/**
 * dev/heaterauto.cpp — /dev/heaterauto: read/write, auto (PID) vs manual
 *
 * "on" = automatic/PID control active, "off" = manual (open-loop).
 * Named so the on/off value reads unambiguously on its own -- "is auto
 * control on?" -- rather than needing you to remember which of two
 * unrelated modes "on" happens to mean. Same on/off vocabulary as
 * /dev/lamp/alarm/relay -- specifically so the existing toggle program
 * works against it completely unchanged:
 *
 *   toggle /dev/buttons/manual /dev/heaterauto &
 *
 * makes the physical MANUAL button flip it, same shape as the lamp's
 * button toggle. Not auto-started from main() -- like the lamp demo,
 * it's a job you start yourself when you want it running; ask if
 * you'd rather it start automatically at boot.
 *
 * Scope, deliberately: this device only stores and reports the mode.
 * It does not yet change how heaterpower gets computed -- /dev/heater
 * still applies whatever's written to it directly, in either mode.
 * Wiring auto mode to an actual control loop is its own, later step.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static bool auto_mode = false; // starts in manual

static int heaterauto_read(char *buf, int len)
{
    return snprintf(buf, len, auto_mode ? "on\n" : "off\n");
}

static int heaterauto_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { auto_mode = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { auto_mode = false; return len; }
    return -1;
}

static const device_t dev_heaterauto = {
    "/dev/heaterauto",
    0,
    0,
    heaterauto_read,
    heaterauto_write,
};

void heaterauto_register(void)
{
    fs_register(&dev_heaterauto);
}
