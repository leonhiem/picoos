#pragma once

#include <stdint.h>
#include "pico/stdlib.h"

#define INTRO_LOGO "KSE Medical Warmer"
#define SW_VERSION "LH20260427"

/*
 * Button constants
 */
#define DEBOUNCE_SHORT 5

#define BUTTON_UP     0
#define BUTTON_DOWN   1
#define BUTTON_MUTE   2
#define BUTTON_MANUAL 3
#define BUTTON_START  4
#define BUTTON_LAMP   5

/* GPIOs */
#define BUTTON_PIN_UP      7
#define BUTTON_PIN_DOWN    8
#define BUTTON_PIN_MUTE    2
#define BUTTON_PIN_MANUAL  9
#define BUTTON_PIN_START  28
#define BUTTON_PIN_LAMP   29

#define PIN_ALARM       0
#define PIN_ALARM_LED   1
#define PIN_SEG_SER     3
#define PIN_SEG_LATCH   4
#define PIN_SEG_SCK     5
#define PIN_LAMP        6
#define PIN_HEATERSAFE  17
#define PIN_SSR_BARGRPH 16
#define PIN_SSR         11

#define BUTTON_COUNT 6

/*
 * LED's on frontpanel -- still used by update_7seg()'s signature.
 */
typedef struct led_s {
    uint8_t aut;
    uint8_t warm;
    uint8_t low;
    uint8_t high;
    uint8_t fail;
    uint8_t chk;
    uint8_t man;
} led_t;

#define ADS1115_I2C_ADDR 0x48  /* I2C address */

/* Time-Proportional Output (TPO) -- tpo_apply() is kept as a low-level
 * primitive (drives PIN_SSR/the PWM bargraph from a 0-100 power value)
 * even though nothing currently sets that value; a future /dev/heater
 * device is the natural thing to wire in front of it. */
#define TPO_PERIOD_MS  15000  /* 15-second window */
#define TICK_MS          100  /* tick used by TPO_TICKS */
#define TPO_TICKS      (TPO_PERIOD_MS / TICK_MS)   /* = 150 ticks */

#define PWM_SLICE_NUM 0
#define PWM_WRAP_VAL 0xFFFF

// ═══════════════════════════════════════════════════
// Shared mutable state -- just the button debounce arrays now.
// Everything else (sensor state, display state) moved into the
// devices that own it (dev/*.cpp) once the control-loop globals
// (tempctl/safecheck/heatercheck/alarm) were retired.
// ═══════════════════════════════════════════════════
extern volatile bool button_pressed[6];      // buttons.cpp
extern volatile bool button[6];              // buttons.cpp
extern volatile int  button_cnt[6];          // buttons.cpp
extern const uint    button_pin[6];          // buttons.cpp
extern uint32_t      time_isr_enter;         // buttons.cpp

// ═══════════════════════════════════════════════════
// Cross-file function prototypes
// ═══════════════════════════════════════════════════
void setup_gpios(void);
void warmer_do_reboot(void);

void isr_enter(uint gpio, uint32_t events);
bool repeating_timer_callback(struct repeating_timer *t);
bool any_button_pressed(void);
void reset_all_buttons(void);

void update_7seg(uint8_t digl3, uint8_t digl2, uint8_t digl1, uint8_t dotl,
                 uint8_t digs3, uint8_t digs2, uint8_t digs1, uint8_t dots, led_t led);
void clear_seg7(void);
void init_seg7(void);

void tpo_apply(void);

// dev/*.cpp -- register each device with kernel/fs.h's namespace.
void skintemp_register(void);
void lamp_register(void);
void buttons_register(void);

// shell.cpp
void task_shell(void);
