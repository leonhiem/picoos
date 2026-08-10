#include "task.h"
#include "pico/time.h"

#define TASK_MAX 8

typedef struct {
    const char *name;
    task_fn_t   fn;
    uint32_t    period_ms;
    absolute_time_t next_run;
} task_t;

static task_t tasks[TASK_MAX];
static int task_count = 0;

void task_register(const char *name, task_fn_t fn, uint32_t period_ms)
{
    if (task_count >= TASK_MAX) return; // TODO: report overflow once we have a console task

    tasks[task_count].name      = name;
    tasks[task_count].fn        = fn;
    tasks[task_count].period_ms = period_ms;
    tasks[task_count].next_run  = make_timeout_time_ms(period_ms);
    task_count++;
}

void task_run(void)
{
    for (int i = 0; i < task_count; i++) {
        if (absolute_time_diff_us(get_absolute_time(), tasks[i].next_run) < 0) {
            tasks[i].next_run = make_timeout_time_ms(tasks[i].period_ms);
            tasks[i].fn();
        }
    }
}
