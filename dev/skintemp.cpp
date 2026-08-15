/**
 * dev/skintemp.cpp — /dev/skintemp: read-only, skin NTC via the ADS1115
 *
 * cat /dev/skintemp -> a fresh reading each time, e.g. "36.5\n". No
 * polling task, no shared global -- the device owns its own hardware
 * state and does a live synchronous read whenever it's actually read.
 * (That read blocks for ~15-20ms on the ADS1115's conversion; fine for
 * an occasional shell command, there's no PID loop left to jitter.)
 */
#include "warmer.h"
#include "kernel/fs.h"
#include "ntc_lut.h"
#include "ads1115.h"
#include "hardware/i2c.h"
#include <cstdio>

static struct ads1115_adc adc;
static bool ready = false;

static int skintemp_open(void)
{
    if (!ready) {
        ads1115_init(i2c0, ADS1115_I2C_ADDR, &adc);
        ads1115_set_pga(ADS1115_PGA_1_024, &adc); // +/- 1V
        ads1115_set_data_rate(ADS1115_RATE_128_SPS, &adc);
        ready = true;
    }
    return 0;
}

static int skintemp_read(char *buf, int len)
{
    ads1115_set_input_mux(ADS1115_MUX_DIFF_0_1, &adc);
    ads1115_write_config(&adc);

    uint16_t adc_value;
    ads1115_read_adc(&adc_value, &adc);

    short adc_sample = (short)adc_value;
    adc_sample += 11025; // 0V -> 50degC
    adc_sample = adc_sample >> 5;
    if (adc_sample < 0)   adc_sample = 0;
    if (adc_sample > 649) adc_sample = 649;

    float t_skin = (float)(ntc_lut[adc_sample]) / 10.0f;
    return snprintf(buf, len, "%.1f\n", t_skin);
}

static const device_t dev_skintemp = {
    "/dev/skintemp",
    skintemp_open,
    0,               // no close needed
    skintemp_read,
    0,               // read-only
};

void skintemp_register(void)
{
    fs_register(&dev_skintemp);
}
