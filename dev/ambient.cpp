/**
 * dev/ambient.cpp — /dev/ambient: read-only, room-temp NTC via the ADS1115
 *
 * Same idea as dev/skintemp.cpp -- the same physical ADS1115, the other
 * differential channel (MUX_DIFF_2_3 instead of _0_1), same NTC
 * conversion (ntc.cpp/ntc.h -- shared with skintemp specifically so
 * ntc_lut.h's unguarded array definition is only ever #included once;
 * two copies link-errors as a duplicate symbol). Its own independent
 * ads1115_adc struct and open(), same as skintemp -- two devices
 * sharing one physical I2C chip is fine under this cooperative
 * scheduler (no read can be interrupted mid-transaction by the other),
 * so there's no shared-hardware-ownership problem to solve, same
 * reasoning skintemp already established.
 *
 * This channel went unbuilt until prog/phase.cpp's safe mode needed a
 * real ambient reading for prog/safelut.cpp's lookup table -- before
 * that there was nothing in this experimental line reading it.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include "ntc.h"
#include "ads1115.h"
#include "hardware/i2c.h"
#include <cstdio>

static struct ads1115_adc adc;
static bool ready = false;

static int ambient_open(void)
{
    if (!ready) {
        ads1115_init(i2c0, ADS1115_I2C_ADDR, &adc);
        ads1115_set_pga(ADS1115_PGA_1_024, &adc); // +/- 1V
        ads1115_set_data_rate(ADS1115_RATE_128_SPS, &adc);
        ready = true;
    }
    return 0;
}

static int ambient_read(char *buf, int len)
{
    ads1115_set_input_mux(ADS1115_MUX_DIFF_2_3, &adc);
    ads1115_write_config(&adc);

    uint16_t adc_value;
    ads1115_read_adc(&adc_value, &adc);

    float t_ambient = ntc_convert(adc_value);
    return snprintf(buf, len, "%.1f\n", t_ambient);
}

static const device_t dev_ambient = {
    "/dev/ambient",
    ambient_open,
    0,               // no close needed
    ambient_read,
    0,               // read-only
};

void ambient_register(void)
{
    fs_register(&dev_ambient);
}
