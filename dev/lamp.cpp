/**
 * dev/lamp.cpp — /dev/lamp: write-only, the baby-light LED
 *
 * write /dev/lamp on   -> lamp on
 * write /dev/lamp off  -> lamp off
 * Anything else is ignored (returns -1: bad command).
 */
#include "warmer.h"
#include "kernel/fs.h"
#include "hardware/gpio.h"
#include <cstring>

static int lamp_write(const char *buf, int len)
{
    // buf isn't guaranteed nul-terminated by the caller; compare by length.
    if (len >= 2 && strncmp(buf, "on", 2) == 0) {
        gpio_put(PIN_LAMP, 1);
        return len;
    }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        gpio_put(PIN_LAMP, 0);
        return len;
    }
    return -1;
}

static const device_t dev_lamp = {
    "/dev/lamp",
    0,             // no open needed -- setup_gpios() already configured PIN_LAMP
    0,             // no close needed
    0,             // write-only
    lamp_write,
};

void lamp_register(void)
{
    fs_register(&dev_lamp);
}
