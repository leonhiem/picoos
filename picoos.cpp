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
    gpio_set_function(PIN_SSR_BARGRPH, GPIO_FUNC_PWM);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config,38); // 125e6/38/65535=50.19415097 Hz
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
    leds_register();
    seg7big_register();
    seg7small_register();
    alarm_register();
    current_register();
    heater_register();
    relay_register();
    setpoint_register();

    cat_register();
    echo_register();
    thresh_register();
    hyst_register();
    ls_register();
    jobs_init();

    task_register("shell",    task_shell,             30); // 30ms: responsive to typing
    task_register("tpo",      tpo_apply,  TPO_INTERVAL_TIME); // applies /dev/heater's value
    task_register("setpoint", task_setpoint_buttons,  150); // polls UP/DOWN for /dev/setpoint

    while(1) {
        task_run();
        watchdog_update();
    }
    return 0;
}
