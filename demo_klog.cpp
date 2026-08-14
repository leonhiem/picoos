/**
 * demo_klog.cpp — picoos step: serialized logging across independent tasks
 *
 * Three producer tasks log at different, deliberately unrelated rates;
 * one console task drains the queue. Proves klog() keeps the output
 * coherent — one complete line at a time, in submission order — no
 * matter how independently the producers are scheduled.
 */
#include <pico/stdlib.h>
#include "kernel/task.h"
#include "kernel/klog.h"

static void task_sensor(void)
{
    static int n = 0;
    klog("sensor", "reading #%d", n++);
}

static void task_control(void)
{
    static int n = 0;
    klog("control", "tick #%d", n++);
}

static void task_ui(void)
{
    static int n = 0;
    klog("ui", "poll #%d", n++);
}

static void task_console(void)
{
    klog_flush();
}

int main()
{
    stdio_init_all();
    sleep_ms(2000); // let the USB CDC connection come up before we start logging

    task_register("sensor",  task_sensor,  700);
    task_register("control", task_control, 400);
    task_register("ui",      task_ui,      1300);
    task_register("console", task_console, 100);

    while (1) {
        task_run();
        sleep_ms(1);
    }
}
