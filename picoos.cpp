/**
 * picoos.cpp — experimental line: Plan 9-flavored device namespace + shell
 *
 * Forked from babywarmer at the point the two diverged (2026-08-15).
 * The PID controller, the phase state machine, and the tasks built
 * around them (task_pidctrl, task_check, task_sensor, task_display,
 * task_input, task_alarm) are gone. What's left: hardware setup
 * (setup_gpios), a cooperative scheduler (kernel/task.h), a device
 * namespace (kernel/fs.h), and an interactive shell (shell.cpp) to
 * poke at it. Devices in dev/ own their own hardware state directly --
 * no shared tempctl/safecheck/heatercheck globals anymore.
 */

#include <pico/stdlib.h>
#include <cstdio>
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "pico/binary_info.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include "jobs.h"

#include "warmer.h"

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
    gpio_put(PIN_ALARM, 0); // silent -- PIN_ALARM was inverted (0 =
                             // sounding) until a 2026-08-26 hardware
                             // change to the buzzer wiring flipped it
                             // back to normal (1 = sounding); see
                             // dev/alarm.cpp

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
    gpio_set_function(PIN_SSR_BARGRPH, GPIO_FUNC_PWM);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, 1); // fastest divider available
    pwm_config_set_wrap(&config, PWM_WRAP_VAL); // ~3kHz -- see warmer.h comment
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

int main()
{
    stdio_init_all();
    setup_gpios();
    init_seg7();
    tft_init();

    // boot on delay
    sleep_ms(2000);

    if (watchdog_caused_reboot()) {
        printf("Rebooted by watchdog\r\n");
    } else {
        printf("Clean boot\r\n");
    }
    printf("%s [%s] startup\r\n",INTRO_LOGO,SW_VERSION);

    // button debounce IRQ
    time_isr_enter = to_ms_since_boot(get_absolute_time());
    gpio_set_irq_enabled_with_callback(button_pin[BUTTON_START], GPIO_IRQ_EDGE_FALL , true, &isr_enter);
    struct repeating_timer timer;
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer);

    watchdog_enable(0x7fffff, 1); // 8 seconds (is max)

    // Populate /dev, then the shell that lets you poke at it.
    skintemp_register();
    lamp_register();
    buttons_register();
    button_devices_register();
    leds_register();
    led_devices_register();
    seg7big_register();
    seg7small_register();
    alarm_register();
    current_register();
    heater_register();
    relay_register();
    setpoint_register();
    heaterauto_register();
    percent_register();
    pid_devices_register();
    ambient_register();
    phase_devices_register();
    alarmcheck_devices_register();
    tft_devices_register();

    cat_register();
    echo_register();
    label_register();
    thresh_register();
    hyst_register();
    toggle_register();
    adjust_register();
    follow_register();
    pid_register();
    monitor_register();
    phase_register();
    safelut_register();
    select_register();
    alarmcheck_register();
    alarmctl_register();
    ledwire_register();
    tftwire_register();
    ls_register();
    jobs_init();

    task_register("shell",    task_shell,            30); // 30ms: responsive to typing
    task_register("tpo",      tpo_apply, TPO_INTERVAL_TIME); // applies /dev/heater's value
    task_register("tftflush", tft_flush_task,         20); // ST7735 redraw/blink -- a
        // task, not a job slot, so it can't be `kill`ed: the display keeps
        // animating even if bin/tftwire (the job) is killed. 20ms is well
        // under display_update()'s own blink periods (250/500ms) -- see
        // dev/tft.cpp.

    while(1) {
        task_run();
        watchdog_update();
    }
    return 0;
}
