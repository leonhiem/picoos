#pragma once

#include <stdint.h>
#include "pico/stdlib.h"
#include "ads1115.h"

#define INTRO_LOGO "KSE Medical Warmer"
#define SW_VERSION "LH20260427"

/*
 * Scheduling periodic times
 */
#define MINUTE_INTERVAL_TIME (60*1000) // 1 min  : counting minutes
#define SENSOR_INTERVAL_TIME (1000)    // 1 s    : reading temperature sensors
#define PID_CTRL_INTERVAL_TIME (1000)  // 1 s    : updating
#define CHECK_INTERVAL_TIME (15*1000)  // 15 s   : safe checking
#define MUTE_INTERVAL_TIME (60*1000)   // 1 min  : muting alarm sound timeout
#define ANIMATION_INTERVAL_TIME (160)  // 160 ms : updating displays, buttons
#define TPO_INTERVAL_TIME       (100)  // 100 ms : updating tpo

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
 * LED's on frontpanel
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

typedef struct alarm_s {
    bool alarm;
    bool muted;
    absolute_time_t muted_time; // armed mute timeout
} alarm_t;

typedef struct setpoint_s {
    float temp;    // temperature setting for automatic control
    float percent; // 0-100% for manual heater control
} setpoint_t;

/* ═══════════════════════════════════════════════════
   NTC / bridge / ADS1115
   ═══════════════════════════════════════════════════ */
#define ADS1115_I2C_ADDR 0x48  /* I2C address */
#define NTC_BETA    3950.0f    /* MF58 β coefficient                */
#define NTC_R25     10000.0f   /* resistance at 25 °C  (Ω)          */
#define BRIDGE_R    10000.0f   /* three fixed bridge resistors  (Ω)  */
#define T25_K        298.15f   /* 25 °C in Kelvin                    */
#define VCC            3.3f    /* bridge supply voltage              */
#define ADS_FSR        2.048f  /* ADS1115 PGA ±2.048 V    TODO FIXME       */
#define ADS_COUNTS 32768.0f    /* 2^15 full-scale counts             */
#define ADS_LSB     (ADS_FSR / ADS_COUNTS)   /* 62.5 µV / count    */

/* ═══════════════════════════════════════════════════
   Setpoints and safety limits
   ═══════════════════════════════════════════════════ */
#define SETPOINT_TEMP_MIN (30.0)
#define SETPOINT_TEMP_MAX (39.0)
#define SETPOINT_TEMP_DEF (35.0)
#define SETPOINT_PCT_DEF  (10.0f)  /* manual mode default heater % */

/* Phase 1A no-rise watchdog */
#define NO_RISE_TIMEOUT_MS  300000  /* 5 minutes                           */
#define NO_RISE_MIN_DELTA    0.3f   /* °C rise required over that window   */

/* ═══════════════════════════════════════════════════
   Time-Proportional Output (TPO)
   ═══════════════════════════════════════════════════ */
#define TPO_PERIOD_MS  15000  /* 15-second window — matches thermal lag  */
#define TICK_MS          100  /* main loop cadence                        */
#define TPO_TICKS      (TPO_PERIOD_MS / TICK_MS)   /* = 150 ticks        */

/* ═══════════════════════════════════════════════════
   PID tuning  (conservative starting point)
   ───────────────────────────────────────────────────
   Feed-forward handles steady-state; PID output is
   intentionally narrow (±25 %) to avoid overshooting.
   ═══════════════════════════════════════════════════ */
#define PID_KP       3.0f   /* % per °C                                */
#define PID_TI     200.0f   /* integral time (s) — was 100 (too fast,  */
                            /* caused wind-up oscillations on slow      */
                            /* thermal plant), then 300 (very stable    */
                            /* but slow ~25 min recovery from trough).  */
                            /* TI=200 gives Kp/Ti = 0.015 %/(°C·s) —    */
                            /* 50 % faster integrator climb without     */
                            /* provoking oscillation.                   */
#define PID_TD       5.0f   /* derivative time (s) — was 40; at 1s/tick */
                            /* and 0.1°C sensor resolution, TD=40 caused */
                            /* ±12% heaterpower spikes per sensor LSB.   */
                            /* TD=5 limits that to ±1.5% per LSB.        */
#define PID_DT       1.0f   //(TICK_MS / 1000.0f)   // 0.1 s
#define PID_OUT_MIN (-35.0f)
#define PID_OUT_MAX  (25.0f)

/* Overall duty cycle bounds */
#define DUTY_MAX        75.0f
#define DUTY_SAFE_CAP   55.0f

/* Heater mode select */
#define HEATER_MODE_PID    1
#define HEATER_MODE_MANUAL 0

/* Safe mode */
#define SENSOR_DROP_RATE    0.08f  // °C/tick — faster than this = suspicious.
                                   // Was 0.5: a displaced sensor on a cool surface
                                   // drops at ~0.04–0.1 °C/s, well below 0.5.
                                   // 0.08 still exceeds any physiological cooling
                                   // rate a baby under active heating can produce.
#define SENSOR_DROP_DELTA   2.0f   // °C below setpoint — secondary confirmation
#define SENSOR_SUSTAIN_TICKS 20    // seconds below (setpoint-3°C) → safe mode
                                   // 60s was too slow for clinical use; 20s is
                                   // still long enough to avoid false triggers
                                   // during normal approach from coast.

/* ═══════════════════════════════════════════════════
   Feed-forward table: room temp → base heater power
   Tune these values during commissioning.
   Linear interpolation is applied between entries.
   ═══════════════════════════════════════════════════ */
typedef struct { float t_room; float pwr_pct; } FFPoint;
/* ═══════════════════════════════════════════════════
   Controller state
   ═══════════════════════════════════════════════════ */
typedef enum {
    STATE_IDLE,       /* entry point */
    STATE_MANUAL,     /* 0-100% manual heaterpower setting */
    STATE_PHASE1A,    /* 100 % boost, wait for skin rise              */
    STATE_COAST,
    STATE_PHASE2,     /* PID + feed-forward, normal operation         */
    STATE_SAFE_MODE,  /* NTC not on skin — lookup table + alert       */
} ControlState;



/* PID state variables */
typedef struct {
    float integral;
    float prev_meas;   /* for derivative-on-measurement               */
} PIDState;



typedef struct wtempctl_s {
    setpoint_t setpoint;
    float t_skin;
    float t_ambient;

    bool skin_ok;
    bool amb_ok;

    bool temphigh;
    bool templow;
    PIDState pid;
    float heaterpower;

    /* Setpoint-change detection */
    float           sp_before_edit;
    absolute_time_t sp_settle_time;
    bool            sp_edit_pending;
} wtempctl_t;

typedef struct wsafecheck_s {
    /* Phase 1A no-rise watchdog */
    int   no_rise_ticks;
    int   coast_ticks;
    float skin_at_window_start;
    bool  window_started;
    bool  warn;
    /* Phase 2 sustained-low watchdog */
    int   below_sp_ticks;          // counts seconds skin stays > 3°C below setpoint
    /* Phase 2 drop-rate watchdog — multi-tick confirmation */
    int   drop_ticks;              // consecutive ticks with drop_rate > threshold
} wsafecheck_t;

typedef struct wheatercheck_s {
#define HEAT_table_LEN 8
    uint16_t curr_sense_table[HEAT_table_LEN];
    int c_idx;
    uint16_t curr_sense;
    bool fail;
} wheatercheck_t;

// PWM slice/wrap used by both setup_gpios() (picoos.cpp) and tpo_apply()
// (heater.cpp). Also (identically) redefined inline in setup_gpios() where
// it originally lived -- harmless, the preprocessor allows an identical
// macro redefinition.
#define PWM_SLICE_NUM 0
#define PWM_WRAP_VAL 0xFFFF

// ═══════════════════════════════════════════════════
// Shared mutable state. Each is owned (single writer) by one
// task/file; others may read via these externs. Defined in the
// file noted; picoos.cpp owns state that's genuinely cross-cutting.
// ═══════════════════════════════════════════════════
extern volatile bool button_pressed[6];      // buttons.cpp
extern volatile bool button[6];               // buttons.cpp
extern volatile int  button_cnt[6];           // buttons.cpp
extern const uint    button_pin[6];           // buttons.cpp
extern uint32_t      time_isr_enter;          // buttons.cpp

extern volatile bool babylight;                // picoos.cpp
extern volatile bool heater_mode;              // picoos.cpp
extern volatile bool timer_started;            // picoos.cpp
extern alarm_t        alarm;                    // picoos.cpp
extern wtempctl_t     tempctl;                  // picoos.cpp
extern wsafecheck_t   safecheck;                // picoos.cpp
extern wheatercheck_t heatercheck;              // picoos.cpp

extern struct ads1115_adc adc;   // sensors.cpp (task_sensor owns it;
                                  // main() also touches it for one-time setup)
extern ControlState state;       // heater.cpp (task_pidctrl is the only writer)
extern bool display_needs_refresh; // display.cpp

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

void heater_check_task(bool first_time);
void tpo_apply(void);

void task_display(void);
void task_input(void);
void task_alarm(void);
void task_minute(void);
void task_sensor(void);
void task_check(void);
void task_pidctrl(void);
