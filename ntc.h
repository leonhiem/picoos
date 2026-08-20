#pragma once
#include <stdint.h>

// Shared ADS1115-raw-to-Celsius conversion for the NTC channels (skin,
// ambient). Factored out of dev/skintemp.cpp so ntc_lut.h (a plain
// array, no `static`/`extern`) is only ever #included once, from
// ntc.cpp -- including it from two translation units caused a
// "multiple definition of ntc_lut" link error the moment
// dev/ambient.cpp needed the same table dev/skintemp.cpp already used.
float ntc_convert(uint16_t adc_value);
