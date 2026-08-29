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

// Digit values for update_7seg()'s dig* args -- 0-9 are literal digits.
#define SEG_BLANK 10
#define SEG_ALL   11

#define ADS1115_I2C_ADDR 0x48  /* I2C address */

/* Time-Proportional Output (TPO) -- tpo_apply() is the low-level
 * primitive (drives PIN_SSR/the PWM bargraph from a 0-100 power value);
 * dev/heater.cpp's heater_set_power() is what sets that value now. */
#define TPO_PERIOD_MS  15000  /* 15-second window */
#define TICK_MS          100  /* tick used by TPO_TICKS */
#define TPO_TICKS      (TPO_PERIOD_MS / TICK_MS)   /* = 150 ticks */
#define TPO_INTERVAL_TIME (100) /* 100 ms: how often tpo_apply() ticks */

#define PWM_SLICE_NUM 0
#define PWM_WRAP_VAL 41666 // top; with clkdiv=1 -> 125e6/1/41667=3000.01 Hz (was 50Hz/0xFFFF)

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
void heater_set_power(float pct); // clamped to [0,100]; dev/heater.cpp calls this
float heater_get_power(void);     // last commanded value; dev/heater.cpp's read() calls this

// dev/*.cpp -- register each device with kernel/fs.h's namespace.
void skintemp_register(void);
void lamp_register(void);
void buttons_register(void);
void button_devices_register(void); // /dev/buttons/<name>, dev/buttons.cpp
void leds_register(void);
void led_devices_register(void); // /dev/leds/<name>, dev/seg7.cpp
void seg7big_register(void);
void seg7small_register(void);
void alarm_register(void);
void current_register(void);
void heater_register(void);
void relay_register(void);
void setpoint_register(void);
void heaterauto_register(void);
void percent_register(void);
void pid_devices_register(void); // /dev/pid/{kp,ti,td,dt,integral,prevmeas}, /dev/pidout, dev/pid.cpp
void ambient_register(void);        // /dev/ambient, dev/ambient.cpp
void phase_devices_register(void);  // /dev/state, /dev/autopower, /dev/safepower, dev/phase.cpp
void alarmcheck_devices_register(void); // /dev/alarm/{heater,temphigh,templow}, dev/alarmcheck.cpp
void tft_devices_register(void); // /dev/tft/{aut,man,chk,low,high,fail,heater}, dev/tft.cpp
void tft_init(void);             // one-time ST7735 bring-up, call once from main()
void tft_flush_task(void);       // periodic redraw/animation, register as a kernel task

// prog/*.cpp -- register each program with kernel/prog.h's registry.
void cat_register(void);
void echo_register(void);
void thresh_register(void);
void hyst_register(void);
void toggle_register(void);
void adjust_register(void);
void follow_register(void);
void pid_register(void);
void monitor_register(void);
void phase_register(void);
void safelut_register(void);
void select_register(void);
void alarmcheck_register(void);
void alarmctl_register(void);
void ledwire_register(void);
void tftwire_register(void);
void ls_register(void);

// shell.cpp
void task_shell(void);
