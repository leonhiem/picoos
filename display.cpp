#include "warmer.h"
#include "hardware/gpio.h"

bool display_needs_refresh=true;

/* Segment and LED lookup tables.
 * Defined here so init_seg7/clear_seg7 can also use SEG_BLANK / SEG_ALL. */
#define SEG_BLANK 10
#define SEG_ALL   11

/* ═══════════════════════════════════════════════════
   update_7seg()
   Shifts 56 bits into the 74HC595 chain (7 bytes):
     byte 7 : digit_l3  (large display, most-significant digit)
     byte 6 : digit_l2  (large display, middle digit + dot)
     byte 5 : digit_l1  (large display, least-significant digit)
     byte 4 : LEDs
     byte 3 : digit_s3  (small display, most-significant digit)
     byte 2 : digit_s2  (small display, middle digit + dot)
     byte 1 : digit_s1  (small display, least-significant digit)

   Timing: no sleep_ms() — gpio_put() on RP2040 takes ~50–100 ns
   through the SIO bus, well above the HC595's 25 ns data-setup
   and 20 ns clock-pulse requirements.  Total update time: ~10 µs.
   ═══════════════════════════════════════════════════ */
void update_7seg(uint8_t digl3, uint8_t digl2, uint8_t digl1, uint8_t dotl,
                 uint8_t digs3, uint8_t digs2, uint8_t digs1, uint8_t dots, led_t led)
{
    //                                      0    1    2    3    4    5    6    7    8    9  BLK  ALL
    static const uint8_t segl_lut[12] = {0xFC,0x60,0xDA,0xF2,0x66,0xB6,0xBE,0xE4,0xFE,0xF6,0x00,0xFF};
    static const uint8_t segs_lut[12] = {0xBE,0x18,0x76,0x7C,0xD8,0xEC,0xEE,0xB8,0xFE,0xFC,0x00,0xFF};

    uint8_t leds = (led.aut  << 6) | (led.warm << 5) | (led.low  << 4) |
                   (led.high << 3) | (led.fail << 2) | (led.chk  << 1) | (led.man);

    /* Pack all 7 bytes into a uint64 for convenient MSB-first shifting.
     * Shifted left 8 so the first bit to clock out is already in bit 63. */
    uint64_t reg_seg =
        ((uint64_t)segl_lut[digl3]       << 56) |
        ((uint64_t)(segl_lut[digl2]|dotl)<< 48) |
        ((uint64_t)segl_lut[digl1]       << 40) |
        ((uint64_t)leds                  << 32) |
        ((uint64_t)segs_lut[digs3]       << 24) |
        ((uint64_t)(segs_lut[digs2]|dots)<< 16) |
        ((uint64_t)segs_lut[digs1]       <<  8);

    /* Clock out 56 bits, MSB first.
     * HC595s replaced 2026-08-10: counterfeit open-collector ICs swapped for
     * genuine totem-pole parts, which no longer add their own inversion —
     * so the bit is sent as-is now (was: !(...) to compensate for the
     * counterfeits). This covers dotl/dots too, since they're packed into
     * reg_seg above rather than going through segl_lut/segs_lut. */
    for (int i = 0; i < 56; i++) {
        gpio_put(PIN_SEG_SER, (reg_seg & 0x8000000000000000ULL) != 0);
        reg_seg <<= 1;
        gpio_put(PIN_SEG_SCK, 1);
        gpio_put(PIN_SEG_SCK, 0);
    }

    /* Latch: rising edge transfers shift register → storage register. */
    gpio_put(PIN_SEG_LATCH, 1);
    gpio_put(PIN_SEG_LATCH, 0);
}

void clear_seg7(void)
{
    led_t led={0,0,0,0,0,0,0};
    update_7seg(SEG_BLANK, SEG_BLANK, SEG_BLANK,0, SEG_BLANK, SEG_BLANK, SEG_BLANK,0, led);
}
void init_seg7(void)
{
    led_t led={1,1,1,1,1,1,1};
    update_7seg(SEG_ALL, SEG_ALL, SEG_ALL,1, SEG_ALL, SEG_ALL, SEG_ALL,1, led);
    sleep_ms(1000);
    clear_seg7();
}


/* ═══════════════════════════════════════════════════
   task_display / task_input / task_alarm
   Split out of the old animation() (which mixed all three
   concerns) plus the alarm-buzzer code that used to sit
   unconditionally in main()'s while(1). Mechanical split --
   each still touches the same variables the same way; no
   ownership/queue changes yet. One deliberate cadence change:
   the buzzer used to be checked every loop spin (effectively
   continuous); it's now on the same 160ms cadence as display/
   input, since it's an operator notification, not a safety
   actuator (PIN_HEATERSAFE/watchdog are handled elsewhere,
   immediately, in task_pidctrl/task_check).
   ═══════════════════════════════════════════════════ */
void task_display(void)
{
    led_t led={0,0,0,0,0,0,0};
    uint8_t digl3;
    uint8_t digl2;
    uint8_t digl1;
    uint8_t digs3;
    uint8_t digs2;
    uint8_t digs1;
    uint8_t dots=0;
    uint8_t dotl=0;

    if(heater_mode == HEATER_MODE_PID) {
        led.aut = 1;
        led.man = 0;
    } else { // HEATER_MODE_MANUAL
        led.aut = 0;
        led.man = 1;
    }
    if(heatercheck.fail) {
        led.fail = 1;
    } else {
        led.fail = 0;
    }
    if(tempctl.heaterpower > 80.0) {
        led.warm = 1;
    }
    led.chk = safecheck.warn;

    led.low = tempctl.templow;
    led.high = tempctl.temphigh;

    if(display_needs_refresh) {
        if(heater_mode == HEATER_MODE_PID) { // tempctl.setpoint.temp

            uint16_t p = (uint16_t)(tempctl.setpoint.temp*10.0);
            digs1=(uint8_t)(p/100);
            if(digs1==0) digs1=SEG_BLANK;
            digs2=(uint8_t)((p/10)%10);
            digs3=(uint8_t)(p%10);
            dots=1;

        } else { // tempctl.setpoint.percent // HEATER_MODE_MANUAL
            uint16_t p = (uint16_t)tempctl.setpoint.percent;
            digs1=(uint8_t)(p/100);
            if(digs1==0) digs1=SEG_BLANK;
            digs2=(uint8_t)((p/10)%10);
            if(digs1==SEG_BLANK && digs2==0) {
                digs2=SEG_BLANK;
            }
            digs3=(uint8_t)(p%10);
            dots=0;
        }

        uint16_t p = (uint16_t)(tempctl.t_skin*10.0);
        digl1=(uint8_t)(p/100);
        if(digl1==0) digl1=SEG_BLANK;
        digl2=(uint8_t)((p/10)%10);
        digl3=(uint8_t)(p%10);
        dotl=1;

        update_7seg(digl3, digl2, digl1,dotl, digs3, digs2, digs1,dots, led);
        display_needs_refresh=false;
    }
}
