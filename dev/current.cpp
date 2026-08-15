/**
 * dev/current.cpp — /dev/current: read-only, heater current-sense ADC
 *
 * cat /dev/current -> a fresh reading each time, raw ADC counts (no
 * documented conversion to amps exists in the codebase this was ported
 * from, so raw counts is the honest thing to report). Averages 8
 * back-to-back samples within the one read() call to knock down noise
 * -- no persistent state, no background polling task, same live-read
 * philosophy as /dev/skintemp.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include "hardware/adc.h"
#include <cstdio>

static int current_read(char *buf, int len)
{
    adc_select_input(0);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    return snprintf(buf, len, "%lu\n", (unsigned long)(sum / 8));
}

static const device_t dev_current = {
    "/dev/current",
    0,
    0,
    current_read,
    0,             // read-only
};

void current_register(void)
{
    fs_register(&dev_current);
}
