/**
 * dev/alarm.cpp — /dev/alarm: write-only, buzzer + alarm LED
 *
 * write /dev/alarm on   -> buzzer + alarm LED on
 * write /dev/alarm off  -> buzzer + alarm LED off
 *
 * PIN_ALARM_LED is inverted in hardware (0 = lit) -- same convention
 * the old task_alarm used, carried over here.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include "hardware/gpio.h"
#include <cstring>

static int alarm_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0) {
        gpio_put(PIN_ALARM, 1);
        gpio_put(PIN_ALARM_LED, 0);
        return len;
    }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        gpio_put(PIN_ALARM, 0);
        gpio_put(PIN_ALARM_LED, 1);
        return len;
    }
    return -1;
}

static const device_t dev_alarm = {
    "/dev/alarm",
    0,
    0,
    0,             // write-only
    alarm_write,
};

void alarm_register(void)
{
    fs_register(&dev_alarm);
}
