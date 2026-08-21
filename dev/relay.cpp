/**
 * dev/relay.cpp — /dev/relay: write-only, the mechanical safety relay
 *
 * write /dev/relay on   -> PIN_HEATERSAFE energized
 * write /dev/relay off  -> PIN_HEATERSAFE released
 *
 * PIN_HEATERSAFE was the safety cutoff in babywarmer's original
 * heater_check_task -- recovered as prog/alarmcheck.cpp, which writes
 * here through this same write() like any other consumer, not by
 * reaching around it. Still a bare manual toggle as far as this device
 * itself is concerned -- it doesn't know or care who's writing it,
 * same deliberately-raw spirit as /dev/heater.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include "hardware/gpio.h"
#include <cstring>

static int relay_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0) {
        gpio_put(PIN_HEATERSAFE, 1);
        return len;
    }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        gpio_put(PIN_HEATERSAFE, 0);
        return len;
    }
    return -1;
}

static const device_t dev_relay = {
    "/dev/relay",
    0,
    0,
    0,             // write-only
    relay_write,
};

void relay_register(void)
{
    fs_register(&dev_relay);
}
