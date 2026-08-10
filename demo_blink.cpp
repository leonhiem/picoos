/**
 * demo_blink.cpp — picoos step 1
 *
 * Two independent tasks, two LEDs, different periods, proving the
 * scheduler dispatches them independently rather than in lockstep.
 * Nothing here talks to the warmer hardware — this is a clean-room demo
 * of the kernel/ scheduler only.
 */
#include <pico/stdlib.h>
#include "kernel/task.h"

#define LED_A_PIN 20
#define LED_B_PIN 21

static void task_blink_a(void)
{
    static bool on = false;
    on = !on;
    gpio_put(LED_A_PIN, on);
}

static void task_blink_b(void)
{
    static bool on = false;
    on = !on;
    gpio_put(LED_B_PIN, on);
}

int main()
{
    stdio_init_all();

    gpio_init(LED_A_PIN);
    gpio_set_dir(LED_A_PIN, GPIO_OUT);
    gpio_init(LED_B_PIN);
    gpio_set_dir(LED_B_PIN, GPIO_OUT);

    task_register("blink_a", task_blink_a, 500); // 1 Hz blink
    task_register("blink_b", task_blink_b, 300); // ~1.7 Hz blink

    while (1) {
        task_run();
        sleep_ms(1); // scheduler is cooperative; this just caps the idle-spin rate
    }
}
