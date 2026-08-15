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
#include "kernel/task.h"


#include "warmer.h"


void setup_gpios(void);

volatile bool babylight;
volatile bool heater_mode;
volatile bool timer_started;
alarm_t alarm;
wtempctl_t tempctl;
wsafecheck_t safecheck;
wheatercheck_t heatercheck;

// display_needs_refresh, adc, state, and the button_* arrays are
// defined in display.cpp / sensors.cpp / heater.cpp / buttons.cpp
// respectively -- declared extern in warmer.h.

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
