/**
 * dev/alarm.cpp — /dev/alarm: write-only, buzzer + alarm LED
 *
 * write /dev/alarm on     -> buzzer + alarm LED on
 * write /dev/alarm muted  -> LED on, buzzer off
 * write /dev/alarm off    -> buzzer + alarm LED off
 *
 * "muted" added alongside the original on/off for prog/alarm.cpp's
 * mute-button handling -- babywarmer's own task_alarm kept the LED lit
 * even while the buzzer was snoozed, so there's always a visible
 * indication, never a fully silent-and-invisible alarm. One more
 * accepted spelling of this device's state, not a second device
 * splitting LED from buzzer -- same reasoning as /dev/heater's on/off
 * aliases sitting alongside its numeric range.
 *
 * PIN_ALARM_LED is inverted in hardware (0 = lit) -- same convention
 * the old task_alarm used, carried over here.
 *
 * PIN_ALARM (the buzzer) was inverted too, on the original board
 * wiring (0 = sounding) -- caught in testing, exactly backwards from
 * babywarmer's own convention (1 = sounding). A 2026-08-26 hardware
 * change to the buzzer circuit flipped its polarity back to that
 * normal (1 = sounding) convention, so PIN_ALARM no longer needs the
 * inversion PIN_ALARM_LED still does.
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
    if (len >= 5 && strncmp(buf, "muted", 5) == 0) {
        gpio_put(PIN_ALARM, 0);
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
