/**
 * warmer.c  —  RP2040 Infant Warmer Controller
 *
 *
 * Control architecture
 * ─────────────────────
 * Phase 1A : 100 % duty — full boost until skin temperature clearly rises.
 *            No-rise watchdog: if skin does not rise ≥ 0.3 °C within
 *            5 min → NTC likely not on baby → enter SAFE MODE.
 *
 *
 * Phase 2  : PID + feed-forward.
 *            Feed-forward (room temp lookup table) carries the steady-state
 *            load; PID corrects only the residual dynamic error.
 *            Derivative acts on measurement (not error) — no kick on start.
 *            Conditional anti-windup: integrator freezes when output saturates.
 *
 * Safe mode: Skin sensor lost or no contact detected.
 *            Power held at room-temp lookup value, capped at 55 %.
 *            Operator alert via buzzer.  Requires power-cycle to exit.
 *
 */

#include <string.h>
#include <time.h>
#include <pico/stdlib.h>
#include <cstdio>
#include <math.h>
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "pico/binary_info.h"
#include "ads1115.h"
#include "ntc_lut.h"
#include "kernel/task.h"


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
const uint  button_pin[6] = { BUTTON_PIN_UP,
                              BUTTON_PIN_DOWN,
                              BUTTON_PIN_MUTE,
                              BUTTON_PIN_MANUAL,
                              BUTTON_PIN_START,
                              BUTTON_PIN_LAMP };

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

static const FFPoint ff_table[] = {
    // Bumped Apr 2026 (round 2): logged steady-state heater at amb=27.7°C
    // was 27.3% with FF=22; bumping ~3% more closes most of the residual
    // gap. Integrator now expected to settle around +2 to +3% rather than
    // +5.5%, with faster climb thanks to TI=200.
    { 10.0f, 50.0f },   // winter northern Vietnam, unchanged (conservative)
    { 12.0f, 48.0f },   // +1 (was +1)
    { 14.0f, 46.0f },   // +2 (was +2)
    { 16.0f, 43.0f },   // +2
    { 18.0f, 40.0f },   // +2
    { 20.0f, 37.0f },   // +2
    { 22.0f, 34.0f },   // +3  (extrapolated)
    { 24.0f, 30.0f },   // +3  (extrapolated)
    { 26.0f, 28.0f },   // +3
    { 28.0f, 25.0f },   // +3  (true SS ~27%, leaves ~2% for integrator)
    { 30.0f, 22.0f },   // +3
    { 32.0f, 18.0f },   // +3
    { 33.0f, 15.0f },   // +3
};
#define FF_LEN  (sizeof(ff_table) / sizeof(ff_table[0]))

static const FFPoint safe_ff_table[] = {
    { 10.0f, 65.0f },
    { 14.0f, 58.0f },
    { 18.0f, 51.0f },
    { 20.0f, 47.0f },
    { 22.0f, 43.0f },
    { 24.0f, 39.0f },
    { 26.0f, 35.0f },
    { 28.0f, 32.0f },
    { 30.0f, 29.0f },
    { 32.0f, 25.0f },
    { 33.0f, 22.0f },
};

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

static const char *state_str[] = {"idl","man","p1A","coa","pid","saf"};


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

void setup_gpios(void);

// button irq begin
volatile bool button_state;
volatile bool button_pressed[6];
volatile bool button[6];
volatile int  button_cnt[6];
//volatile int  any_button_pressed_delay;

volatile bool babylight;
volatile bool heater_mode;
volatile bool timer_started;
alarm_t alarm;
wtempctl_t tempctl;
wsafecheck_t safecheck;
wheatercheck_t heatercheck;

// Owned by task_sensor (periodic reads); initialised once in main() before
// the scheduler starts, so it needs file scope rather than living in either
// function alone.
static struct ads1115_adc adc;

// Owned by task_pidctrl (the only writer); read by task_sensor for its
// telemetry line.
static ControlState state = STATE_IDLE;

// Debounce control
uint32_t time_isr_enter;
const int delayTime = 1000; // Delay for every push button may vary

void isr_enter(uint gpio, uint32_t events) {
    if ((to_ms_since_boot(get_absolute_time())-time_isr_enter)>delayTime) {
        // Recommend to not to change the position of this line
        time_isr_enter = to_ms_since_boot(get_absolute_time());
        
        // Interrupt function lines
        //button_state = !button_state;
        //gpio_put(LED_PIN, button_state);
        //button_event_enter=true;
    }
}
// button irq end

bool any_button_pressed(void)
{
    return (button[BUTTON_UP] ||
            button[BUTTON_DOWN] ||
            button[BUTTON_MUTE] ||
            button[BUTTON_MANUAL] ||
            button[BUTTON_START] ||
            button[BUTTON_LAMP]);
}
void reset_all_buttons(void) 
{
    button[BUTTON_UP]=false;
    button[BUTTON_DOWN]=false;
    button[BUTTON_MUTE]=false;
    button[BUTTON_MANUAL]=false;
    button[BUTTON_START]=false;
    button[BUTTON_LAMP]=false;
}


volatile bool timer_fired = false;
 
int64_t alarm_callback(alarm_id_t id, void *user_data) {
    printf("Timer %d fired!\n", (int) id);
    timer_fired = true;
    // Can return a value here in us to fire in the future
    return 0;
}

bool repeating_timer_callback(struct repeating_timer *t) 
{
    int i;
    for(i=0;i<BUTTON_COUNT;i++) {
        if((gpio_get(button_pin[i]) == false) && button_pressed[i]==false && button_cnt[i]==0) {
            button_pressed[i]=true; button[i]=true; button_cnt[i]=DEBOUNCE_SHORT;
            //any_button_pressed_delay=5000;
        } else if((gpio_get(button_pin[i]) == true) && button_pressed[i]==true && button_cnt[i]==0) {
            button_pressed[i]=false; button_cnt[i]=DEBOUNCE_SHORT;
        } else if(button_cnt[i]>0) button_cnt[i]--;
    }
    return true;
}
 
void setup_gpios(void)
{
    // Buttons
    gpio_init(BUTTON_PIN_UP);
    gpio_pull_up(BUTTON_PIN_UP);
    gpio_init(BUTTON_PIN_DOWN);
    gpio_pull_up(BUTTON_PIN_DOWN);
    gpio_init(BUTTON_PIN_MUTE);
    gpio_pull_up(BUTTON_PIN_MUTE);
    gpio_init(BUTTON_PIN_MANUAL);
    gpio_pull_up(BUTTON_PIN_MANUAL);
    gpio_init(BUTTON_PIN_START);
    gpio_pull_up(BUTTON_PIN_START);
    gpio_init(BUTTON_PIN_LAMP);
    gpio_pull_up(BUTTON_PIN_LAMP);

    // outputs
    gpio_init(PIN_ALARM);
    gpio_set_dir(PIN_ALARM, GPIO_OUT);
    gpio_put(PIN_ALARM, 0);

    gpio_init(PIN_ALARM_LED);
    gpio_set_dir(PIN_ALARM_LED, GPIO_OUT);
    gpio_put(PIN_ALARM_LED, 0);

    gpio_init(PIN_SEG_SER);
    gpio_set_dir(PIN_SEG_SER, GPIO_OUT);
    gpio_put(PIN_SEG_SER, 0);

    gpio_init(PIN_SEG_LATCH);
    gpio_set_dir(PIN_SEG_LATCH, GPIO_OUT);
    gpio_put(PIN_SEG_LATCH, 0);

    gpio_init(PIN_SEG_SCK);
    gpio_set_dir(PIN_SEG_SCK, GPIO_OUT);
    gpio_put(PIN_SEG_SCK, 0);

    gpio_init(PIN_LAMP);
    gpio_set_dir(PIN_LAMP, GPIO_OUT);
    gpio_put(PIN_LAMP, 0);

    gpio_init(PIN_HEATERSAFE);
    gpio_set_dir(PIN_HEATERSAFE, GPIO_OUT);
    gpio_put(PIN_HEATERSAFE, 0);

    gpio_init(PIN_SSR);
    gpio_set_dir(PIN_SSR, GPIO_OUT);
    gpio_put(PIN_SSR, 0);

    // adc
    adc_init();
    adc_gpio_init(26); // current sense
    adc_set_temp_sensor_enabled(true);

    // ADC
    i2c_init(i2c0, 200000);
    gpio_set_function(12, GPIO_FUNC_I2C);
    gpio_set_function(13, GPIO_FUNC_I2C);

    // heater output
#define PWM_SLICE_NUM 0
    gpio_set_function(PIN_SSR_BARGRPH, GPIO_FUNC_PWM);
    // Find out which PWM slice is connected to GPIO (it is 0)

    // 50 Hz
#define PWM_WRAP_VAL 0xFFFF
//    pwm_set_clkdiv_int_frac (PWM_SLICE_NUM,  38,3);
//    pwm_set_wrap(PWM_SLICE_NUM, PWM_WRAP_VAL);


    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config,38); // 125e6/38/65535=50.19415097 Hz
    //pwm_config_set_clkdiv_int(&config,2); //~ 1kHz
    //pwm_config_set_clkdiv(&config, 4.f);
    pwm_init(PWM_SLICE_NUM, &config, true);

    pwm_set_chan_level(PWM_SLICE_NUM, PWM_CHAN_A, 0);
    pwm_set_enabled(PWM_SLICE_NUM, true);
}

void warmer_do_reboot(void)
{
    printf("Reboot now!\n");
    watchdog_enable(100, 1);
    while(1);
}

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

static float feedforward_lookup(float t_room,
                                const FFPoint *table, size_t len)
{
    if (t_room <= table[0].t_room)
        return table[0].pwr_pct;
    if (t_room >= table[len - 1].t_room)
        return table[len - 1].pwr_pct;

    for (size_t i = 0; i < len - 1; i++) {
        if (t_room < table[i + 1].t_room) {
            float span = table[i + 1].t_room - table[i].t_room;
            float frac = (t_room - table[i].t_room) / span;
            return table[i].pwr_pct
                 + frac * (table[i + 1].pwr_pct - table[i].pwr_pct);
        }
    }
    return 50.0f;
}




static float feedforward(float t_room) {
    return feedforward_lookup(t_room, ff_table,
                              sizeof(ff_table) / sizeof(ff_table[0]));
}
static float safe_feedforward(float t_room) {
    return feedforward_lookup(t_room, safe_ff_table,
                              sizeof(safe_ff_table) / sizeof(safe_ff_table[0]));
}

/* ═══════════════════════════════════════════════════
   pid_update()
   Returns output in % clamped to [PID_OUT_MIN, PID_OUT_MAX].

   Design choices:
   • Derivative-on-measurement: avoids kick when entering Phase 2.
   • Conditional anti-windup: integral frozen when output saturates,
     so it cannot accumulate during the unavoidable thermal dead-time.
   ═══════════════════════════════════════════════════ */
static float pid_update(PIDState *s, float setpoint, float measurement)
{
    float error  = setpoint - measurement;
    float p_term = PID_KP * error;

    /* Derivative on measurement (sign: negative because dErr/dt = -dMeas/dt) */
    float d_term = -PID_KP * PID_TD * (measurement - s->prev_meas) / PID_DT;

    /* Tentative integral update */
    float i_new   = s->integral + (PID_KP / PID_TI) * error * PID_DT;
    float output  = p_term + i_new + d_term;

    /* Clamp output */
    float out_clamped = output;
    if (out_clamped > PID_OUT_MAX) out_clamped = PID_OUT_MAX;
    if (out_clamped < PID_OUT_MIN) out_clamped = PID_OUT_MIN;

    /* Anti-windup: only accept integral update if not saturating */
    if (output >= PID_OUT_MIN && output <= PID_OUT_MAX)
        s->integral = i_new;
    /* else: s->integral stays frozen at previous value              */

    s->prev_meas = measurement;
    return out_clamped;
}
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}


/* ═══════════════════════════════════════════════════
   tpo_apply()
   Time-Proportional Output — call once per TICK_MS.
   Converts heaterpower (0–100) to SSR on/off pattern
   over a TPO_PERIOD_MS window.
   ═══════════════════════════════════════════════════ */
static void tpo_apply(void)
{
    static int tick = 0;

    int on_ticks = (int)((tempctl.heaterpower / 100.0f) * TPO_TICKS + 0.5f);
    gpio_put(PIN_SSR, (tick < on_ticks) ? 1 : 0);
    if (++tick >= TPO_TICKS) tick = 0;


    // update PWM bargraph display
    float hp = tempctl.heaterpower * (float)PWM_WRAP_VAL / 100.0;
    pwm_set_chan_level(PWM_SLICE_NUM, PWM_CHAN_A, (uint16_t)(hp));
}


/*
 * Check the status of the heater
 */
void heater_check_task(bool first_time)
{
    heatercheck.fail = false;

    if(tempctl.t_skin > (tempctl.setpoint.temp+3.0)) {
        gpio_put(PIN_HEATERSAFE, 0); // skin temperature is way too high
        return;
    }
    if(tempctl.heaterpower > 0.0) {
        gpio_put(PIN_HEATERSAFE, 1); // turn on safety relay
    }
    if(tempctl.heaterpower > 50.0) {
        if(heatercheck.curr_sense < 50 && !first_time) {
            heatercheck.fail = true;
        }
    } else if(tempctl.heaterpower == 0.0) {
        if(heatercheck.curr_sense > 50 && !first_time) {
            heatercheck.fail = true;
            gpio_put(PIN_HEATERSAFE, 0); // if current is not zero, and heater supposed to be OFF
                                         // turn off safety relay
                                         // because the SSR might be failing and latched ON
        }
    }
}

/*
 * Show all information on displays
 */
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

void task_input(void)
{
    if(button[BUTTON_UP]) {
        button[BUTTON_UP]=false;
        if(heater_mode == HEATER_MODE_PID) { // tempctl.setpoint.temp
            if (!tempctl.sp_edit_pending) {                          // ← capture start-of-session
                tempctl.sp_before_edit  = tempctl.setpoint.temp;
                tempctl.sp_edit_pending = true;
            }
            tempctl.sp_settle_time = make_timeout_time_ms(5000);    // ← (re)arm 5 s window

            if(tempctl.setpoint.temp < SETPOINT_TEMP_MAX) {
                tempctl.setpoint.temp = tempctl.setpoint.temp+0.5;
            }
        } else { // tempctl.setpoint.percent // HEATER_MODE_MANUAL
            if(tempctl.setpoint.percent < 80.0) {
                tempctl.setpoint.percent = tempctl.setpoint.percent+5.0;
            }
        }

        display_needs_refresh=true;
    } else if(button[BUTTON_DOWN]) {
        button[BUTTON_DOWN]=false;
        if(heater_mode == HEATER_MODE_PID) { // tempctl.setpoint.temp
            if (!tempctl.sp_edit_pending) {                          // ← capture start-of-session
                tempctl.sp_before_edit  = tempctl.setpoint.temp;
                tempctl.sp_edit_pending = true;
            }
            tempctl.sp_settle_time = make_timeout_time_ms(5000);    // ← (re)arm 5 s window

            if(tempctl.setpoint.temp > SETPOINT_TEMP_MIN) {
                tempctl.setpoint.temp = tempctl.setpoint.temp-0.5;
            }
        } else { // tempctl.setpoint.percent // HEATER_MODE_MANUAL
            if(tempctl.setpoint.percent > 0.0) {
                tempctl.setpoint.percent = tempctl.setpoint.percent-5.0;
            }
        }
        display_needs_refresh=true;
    } else if(button[BUTTON_MUTE]) {
        button[BUTTON_MUTE]=false;
        alarm.muted=true;
        alarm.muted_time = make_timeout_time_ms(MUTE_INTERVAL_TIME);
    } else if(button[BUTTON_MANUAL]) {
        button[BUTTON_MANUAL]=false;
        heater_mode=!heater_mode; // toggle HEATER_MODE_PID <--> HEATER_MODE_MANUAL
        if(heater_mode == HEATER_MODE_MANUAL) {
            // Customer requested: manual mode always starts at default %, no
            // memory of the previously-set value across mode transitions.
            tempctl.setpoint.percent = SETPOINT_PCT_DEF;
        }
        display_needs_refresh=true;
    } else if(button[BUTTON_START]) {
        button[BUTTON_START]=false;
        timer_started=!timer_started;
        display_needs_refresh=true;
    } else if(button[BUTTON_LAMP]) {
        button[BUTTON_LAMP]=false;
        babylight=!babylight;
        if(babylight) gpio_put(PIN_LAMP, 1); 
        else gpio_put(PIN_LAMP, 0);
    }
}

void task_alarm(void)
{
    //if(safecheck.warn || tempctl.templow || tempctl.temphigh || heatercheck.fail) {
    if( tempctl.templow || tempctl.temphigh || heatercheck.fail) {
        alarm.alarm = true;
    } else {
        alarm.alarm = false;
    }

        if(alarm.alarm) {
            gpio_put(PIN_ALARM_LED, 0); // PIN_ALARM_LED is inverted in hardware

            if(absolute_time_diff_us(get_absolute_time(),alarm.muted_time) < 0) {
                alarm.muted =false;
            }

            if(alarm.muted) {
                gpio_put(PIN_ALARM, 0);
            } else {
                gpio_put(PIN_ALARM, 1);
            }
        } else {
            gpio_put(PIN_ALARM_LED, 1);
            gpio_put(PIN_ALARM, 0);
            alarm.muted=false;
        }
}




/* ═══════════════════════════════════════════════════
   Task functions — registered with the cooperative
   scheduler (kernel/task.h) in main(). Bodies moved
   verbatim from the old hand-rolled while(1) loop;
   indentation kept as-is (one level deeper than a
   normal function body) to avoid any risk of an
   automated re-indent altering this logic — cosmetic
   cleanup can happen once this is verified on hardware.
   ═══════════════════════════════════════════════════ */
void task_minute(void)
{
    static uint32_t warmer_uptime = 0;

    warmer_uptime++;
    //printf("warmer uptime is %d minutes\n",warmer_uptime);
}

void task_sensor(void)
{
    static int line_idx = 0;
    int i;

            // Read sensor task

            // Read heater current from ADC
            adc_select_input(0);
            uint16_t adc_result = adc_read();
            heatercheck.curr_sense_table[heatercheck.c_idx] = adc_result;
            heatercheck.c_idx++;
            heatercheck.c_idx&=(HEAT_table_LEN-1);
            heatercheck.curr_sense = 0;
            for(i=0;i<HEAT_table_LEN;i++) {
                heatercheck.curr_sense += heatercheck.curr_sense_table[i];
            }
            heatercheck.curr_sense = heatercheck.curr_sense / HEAT_table_LEN;

            // Read temperature sensors
            uint16_t adc_value;
            short adc_sample;
    


            ads1115_set_input_mux(ADS1115_MUX_DIFF_0_1, &adc);
            ads1115_write_config(&adc);
            ads1115_read_adc(&adc_value, &adc);
            adc_sample=(short)adc_value;
            adc_sample+=11025; // 0V -> 50degC
            adc_sample=adc_sample>>5;

            tempctl.temphigh = false;
            tempctl.templow = false;

            if(adc_sample<0) {
                adc_sample=0;
                tempctl.temphigh = true;
            }
            if(adc_sample>649) {
                adc_sample=649;
                tempctl.templow = true;
            }
            tempctl.t_skin = (float)(ntc_lut[adc_sample]);
            tempctl.t_skin /= 10.0;

            ads1115_set_input_mux(ADS1115_MUX_DIFF_2_3, &adc);
            ads1115_write_config(&adc);
            ads1115_read_adc(&adc_value, &adc);
            adc_sample=(short)adc_value;
            adc_sample+=11025; // 0V -> 50degC
            adc_sample=adc_sample>>5;

            if(adc_sample<0) {
                adc_sample=0;
            }
            if(adc_sample>649) {
                adc_sample=649;
            }
            tempctl.t_ambient = (float)(ntc_lut[adc_sample]);
            tempctl.t_ambient /= 10.0;


            if(tempctl.t_skin > (tempctl.setpoint.temp+2.0)) tempctl.temphigh = true;
            if(tempctl.t_skin < (tempctl.t_ambient-2.0)) tempctl.templow = true;


            tempctl.skin_ok = true;
            tempctl.amb_ok = true;
            display_needs_refresh=true;

            // Display telemetry
            // pid_i and pid_pm are only meaningful in [pid] state, but logged
            // unconditionally so columns line up across phases.
            if((line_idx % 10)==0) {
                printf("state setp skin amb  heat  low  high  curr fail warn pid_i  pid_pm\n");
            }
            line_idx++;
            printf("[%s] %.1f %.1f %.1f %.1f   %d     %d   %d   %d   %d   %+.2f  %.2f\n",
                   state_str[state],tempctl.setpoint.temp,
                   tempctl.t_skin,tempctl.t_ambient,tempctl.heaterpower,
                   tempctl.templow,tempctl.temphigh,heatercheck.curr_sense,
                   heatercheck.fail,safecheck.warn,
                   tempctl.pid.integral, tempctl.pid.prev_meas);
}

void task_check(void)
{
    static bool first = true;

    heater_check_task(first);
    first = false;
}

void task_pidctrl(void)
{
            if (tempctl.sp_edit_pending &&
                absolute_time_diff_us(get_absolute_time(), tempctl.sp_settle_time) < 0) {
            
                tempctl.sp_edit_pending = false;
                float delta = tempctl.setpoint.temp - tempctl.sp_before_edit;
            
                if (delta >= 2.0f && state == STATE_PHASE2) {
                    // Large upward step — skin needs active warmup
                    safecheck.window_started       = false;
                    safecheck.no_rise_ticks        = 0;
                    safecheck.skin_at_window_start = tempctl.t_skin;
                    safecheck.coast_ticks          = 0;
                    safecheck.below_sp_ticks       = 0;
                    safecheck.drop_ticks           = 0;
                    state = STATE_PHASE1A;
                    printf("warmer -> Phase 1A: setpoint %.1f -> %.1f\n",
                           tempctl.sp_before_edit, tempctl.setpoint.temp);
                }
                // Small increase or any decrease: PID handles it on its own
            }



            switch (state) {
            case STATE_IDLE:
               if(tempctl.amb_ok && tempctl.skin_ok) {
                   if(heater_mode == HEATER_MODE_PID) {
                       state = STATE_PHASE1A;
                   } else { // HEATER_MODE_MANUAL
                       state = STATE_MANUAL;
                   }
               }
               // reset pid
               tempctl.pid.integral = 0.0f;
               tempctl.pid.prev_meas = tempctl.t_skin;
               // restart safe-mode
               safecheck.skin_at_window_start = tempctl.t_skin;
               safecheck.no_rise_ticks        = 0;
               safecheck.warn = false;
               break;
            case STATE_MANUAL:
               if(heater_mode == HEATER_MODE_PID) {
                   state = STATE_IDLE;
               } else { // HEATER_MODE_MANUAL
                   tempctl.heaterpower = tempctl.setpoint.percent;
               }
               break;
   
            case STATE_PHASE1A:
               /* PHASE 1A — 80 % boost
                  Heats aggressively.  At Vietnam ambient (25–30 °C) and a
                  35 °C target, 40 % proved insufficient; full power was
                  excessive (left mattress with too much stored thermal
                  energy → ~1 °C overshoot after coast). 80 % is a middle
                  ground: still rapid warmup, less stored momentum.
   
                  No-rise watchdog: if skin does not climb ≥ NO_RISE_MIN_DELTA
                  within NO_RISE_TIMEOUT_MS the NTC is likely not on the baby.
                */
               if(heater_mode == HEATER_MODE_MANUAL) {
                   state = STATE_IDLE;
               } else { // HEATER_MODE_PID
                   tempctl.heaterpower = 80.0f;
   
                   if (!safecheck.window_started && tempctl.skin_ok) {
                       safecheck.skin_at_window_start = tempctl.t_skin;
                       safecheck.window_started       = true;
                   }
                   safecheck.no_rise_ticks++;

                   if (safecheck.no_rise_ticks >= (NO_RISE_TIMEOUT_MS / TICK_MS)) {
                       float rise = tempctl.skin_ok ? (tempctl.t_skin - safecheck.skin_at_window_start) : 0.0f;
                       if (rise < NO_RISE_MIN_DELTA) {
                           state = STATE_SAFE_MODE;
                           printf("warmer -> SAFE MODE: no skin rise (%.2f C in %d s)\n",
                                  rise, NO_RISE_TIMEOUT_MS / 1000);
                       } else {
                           /* Rise confirmed — restart watchdog window */
                           safecheck.skin_at_window_start = tempctl.t_skin;
                           safecheck.no_rise_ticks        = 0;
                       }
                   }
   
                   if (tempctl.skin_ok && tempctl.t_skin >= (tempctl.setpoint.temp-3.0f)) {
                       state = STATE_COAST;
                       task_postpone("check", CHECK_INTERVAL_TIME); // postpone heater_check_task()
                       printf("warmer -> Phase coast\n");
                   }
               }
               break;
   
            case STATE_COAST:
                if(heater_mode == HEATER_MODE_MANUAL) {
                    state = STATE_IDLE;
                } else { // HEATER_MODE_PID
                    tempctl.heaterpower = 0.0f;
                    safecheck.coast_ticks++;
                    if (safecheck.coast_ticks >= 45) {  // 45 seconds at 1Hz
                        // Seed integrator at +3% rather than 0 to give the
                        // controller a head start. Logged data (Apr 2026)
                        // showed steady-state integrator settling at ~+6.6%
                        // at amb=27°C with the bumped FF table; seeding +3
                        // closes most of that gap immediately while keeping
                        // initial heat injection conservative (skin is still
                        // rising at COAST exit, so P-term already contributes
                        // heat — no need for the integrator to overdo it).
                        // Without this seed the integrator climbed slowly at
                        // Kp/Ti = 0.01 %/(°C·s), causing a 20+ minute approach
                        // to setpoint after overshoot.
                        //
                        // This seed only applies on cold-start and large
                        // upward-setpoint changes (which both route through
                        // PHASE1A → COAST → PHASE2). Small setpoint changes
                        // and decreases stay in PHASE2 and preserve their
                        // already-converged integrator value.
                        tempctl.pid.integral  = 3.0f;
                        tempctl.pid.prev_meas = tempctl.t_skin;
                        safecheck.below_sp_ticks = 0;
                        safecheck.drop_ticks     = 0;
                        state = STATE_PHASE2;
                        printf("warmer -> Phase 2 (PID active, integral seeded at %.1f)\n",
                               tempctl.pid.integral);
                    }
                }
                break;

            case STATE_PHASE2:
                if(heater_mode == HEATER_MODE_MANUAL) {
                    state = STATE_IDLE;
                } else { // HEATER_MODE_PID
                    if (!tempctl.skin_ok) {
                        state = STATE_SAFE_MODE;
                        printf("warmer -> SAFE MODE: skin sensor lost\n");
                        break;
                    }
                
                    // Detect sensor displaced from baby:
                    // (a) rapid drop sustained over 3 consecutive ticks AND
                    //     significantly below setpoint, OR
                    // (b) sustained significantly below setpoint (slow displacement)
                    //
                    // Note: sensor LSB = 0.1 °C. A single noise tick gives
                    // drop_rate = 0.10 °C/s. Multi-tick confirmation prevents
                    // a single quantization step from triggering safe mode.
                    {
                        float drop_rate = tempctl.pid.prev_meas - tempctl.t_skin; // positive = falling
                        bool dropping_fast = (drop_rate > SENSOR_DROP_RATE);
                        bool far_below_sp  = (tempctl.t_skin < (tempctl.setpoint.temp - SENSOR_DROP_DELTA));

                        if (dropping_fast && far_below_sp) {
                            if (++safecheck.drop_ticks >= 3) {
                                state = STATE_SAFE_MODE;
                                printf("warmer -> SAFE MODE: sensor drop detected (%.2f C/s, %d ticks)\n",
                                       drop_rate, safecheck.drop_ticks);
                                safecheck.drop_ticks = 0;
                                break;
                            }
                        } else {
                            safecheck.drop_ticks = 0;  // reset on any non-dropping tick
                        }

                        // Sustained-low watchdog:
                        // A sensor lying on a cool surface may drift slowly — not
                        // fast enough to trip the rate check above, but it will stay
                        // persistently below setpoint. If skin remains > 3°C below
                        // the setpoint for SENSOR_SUSTAIN_TICKS consecutive seconds,
                        // the heater is clearly not warming anything, so enter safe mode.
                        if (tempctl.t_skin < (tempctl.setpoint.temp - 3.0f)) {
                            if (++safecheck.below_sp_ticks >= SENSOR_SUSTAIN_TICKS) {
                                state = STATE_SAFE_MODE;
                                printf("warmer -> SAFE MODE: sustained low skin (%.1f C for %d s)\n",
                                       tempctl.t_skin, SENSOR_SUSTAIN_TICKS);
                                break;
                            }
                        } else {
                            safecheck.below_sp_ticks = 0;  // reset on recovery
                        }
                    }
                
                    // Normal PID update
                    {
                        float ff      = tempctl.amb_ok ? feedforward(tempctl.t_ambient) : 50.0f;
                        float pid_out = pid_update(&tempctl.pid, tempctl.setpoint.temp, tempctl.t_skin);
                        tempctl.heaterpower = clampf(ff + pid_out, 0.0f, DUTY_MAX);
                    }
                }
                break;



   
           case STATE_SAFE_MODE:
              /* SAFE MODE — skin NTC not touching baby, or sensor lost.
                 Maintains moderate warmth via ambient lookup.
                 Alerts operator with periodic beep.
                 Still attempting to restore to PID mode by testing the skin temp
               */
               if(heater_mode == HEATER_MODE_MANUAL) {
                   state = STATE_IDLE;
               } else { // HEATER_MODE_PID
                   safecheck.warn = true;

                   tempctl.heaterpower = clampf(
                       tempctl.amb_ok ? safe_feedforward(tempctl.t_ambient) : 40.0f,
                       0.0f, DUTY_SAFE_CAP);

                   // Recovery requires skin >= setpoint-2 (not setpoint-3) to
                   // provide hysteresis against the watchdog trigger threshold.
                   // Without this gap, a sensor hovering near setpoint-3 could
                   // oscillate rapidly between safe mode and PID.
                   if (tempctl.skin_ok && tempctl.t_skin >= (tempctl.setpoint.temp - 2.0f)) {
                       tempctl.pid.prev_meas = tempctl.t_skin;
                       tempctl.pid.integral  = 0.0f;
                       safecheck.warn = false;
                       state = STATE_PHASE2;
                       printf("warmer safe -> Phase 2 (PID active)\n");
                   }
               }
               break;
   
           default:
               state = STATE_IDLE;
               break;
           }
}


int main() 
{
    memset((void *)button,0,BUTTON_COUNT);
    memset((void *)button_pressed,0,sizeof(button_pressed));
    memset((void *)button_cnt,0,sizeof(button_cnt));
    memset((void *)&tempctl,0,sizeof(wtempctl_t));
    memset((void *)&safecheck,0,sizeof(wsafecheck_t));
    memset((void *)&heatercheck,0,sizeof(wheatercheck_t));
    //any_button_pressed_delay=0;
    
    state = STATE_IDLE;

    // some move to EEPROM later !!!
    babylight = 0;
    heater_mode = HEATER_MODE_PID; 
    timer_started = 0;
    alarm.alarm = false;
    alarm.muted = false;
    alarm.muted_time = get_absolute_time(); // initially safe

    tempctl.setpoint.temp = SETPOINT_TEMP_DEF;
    tempctl.setpoint.percent = SETPOINT_PCT_DEF;

    stdio_init_all();
    setup_gpios();
    init_seg7();


    // Initialise ADC
    ads1115_init(i2c0, ADS1115_I2C_ADDR, &adc);
    ads1115_set_input_mux(ADS1115_MUX_DIFF_0_1, &adc);
    //ads1115_set_pga(ADS1115_PGA_4_096, &adc); // +/- 4V
    //ads1115_set_pga(ADS1115_PGA_2_048, &adc); // +/- 2V // default
    ads1115_set_pga(ADS1115_PGA_1_024, &adc); // +/- 1V  TODO FIXME
    ads1115_set_data_rate(ADS1115_RATE_128_SPS, &adc); // default
    ads1115_write_config(&adc);

    // Required bugfix in: ~/pico/pico-ads1115/lib/ads1115.cpp line 25:
    //    while ((adc->config & ADS1115_STATUS_MASK) == ADS1115_STATUS_BUSY){
    // ---> added the '(' and ')' !!
    // otherwise both t_skin and t_ambient show the same value


    // boot on delay
    sleep_ms(2000);

    if (watchdog_caused_reboot()) {
        printf("Rebooted by watchdog\r\n");
    } else {
        printf("Clean boot\r\n");
    }
    printf("%s [%s] startup\r\n",INTRO_LOGO,SW_VERSION);

    printf("PWM slice number %d\n",pwm_gpio_to_slice_num(PIN_SSR_BARGRPH));
    printf("PWM channel number %d\n",pwm_gpio_to_channel(PIN_SSR_BARGRPH));

    // interrupt service for apgar start timer
    time_isr_enter = to_ms_since_boot(get_absolute_time());
    gpio_set_irq_enabled_with_callback(button_pin[BUTTON_START], GPIO_IRQ_EDGE_FALL , true, &isr_enter);
    struct repeating_timer timer;
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer);

    watchdog_enable(0x7fffff, 1); // 8 seconds (is max)
    // Register periodic tasks with the cooperative scheduler
    // (kernel/task.h). Same functions, same intervals as before this
    // step -- replaces the hand-rolled absolute_time_t deadlines with
    // task_register()/task_run().
    task_register("display", task_display, ANIMATION_INTERVAL_TIME);
    task_register("input",   task_input,   ANIMATION_INTERVAL_TIME);
    task_register("alarm",   task_alarm,   ANIMATION_INTERVAL_TIME);
    task_register("minute",    task_minute,  MINUTE_INTERVAL_TIME);
    task_register("sensor",    task_sensor,  SENSOR_INTERVAL_TIME);
    task_register("check",     task_check,   CHECK_INTERVAL_TIME);
    task_register("tpo",       tpo_apply,    TPO_INTERVAL_TIME);
    task_register("pidctrl",   task_pidctrl, PID_CTRL_INTERVAL_TIME);



    /* ════════════════════════════════════════════
       Main control loop
       ════════════════════════════════════════════ */
    while(1) {
        task_run();
        watchdog_update();
    }
    return 0;
}
