#include "ntc.h"
#include "ntc_lut.h"

float ntc_convert(uint16_t adc_value)
{
    short adc_sample = (short)adc_value;
    adc_sample += 11025; // 0V -> 50degC
    adc_sample = adc_sample >> 5;
    if (adc_sample < 0)   adc_sample = 0;
    if (adc_sample > 649) adc_sample = 649;

    return (float)(ntc_lut[adc_sample]) / 10.0f;
}
