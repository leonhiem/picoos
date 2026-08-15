/**
 * heater.cpp — low-level heater output only
 *
 * The PID controller, the phase state machine (task_pidctrl), the
 * feed-forward tables, and the current-sense safety check
 * (heater_check_task/task_check) are gone -- this is the experimental
 * line, ripped out on purpose. What's left is tpo_apply(): the actual
 * hardware primitive that turns a 0-100 power value into SSR on/off
 * timing and a PWM bargraph level. Nothing currently sets that power
 * value; a future /dev/heater device is the natural thing to call this
 * from.
 */
#include "warmer.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

static float heaterpower = 0.0f; // 0-100; will get a real writer once
                                  // /dev/heater exists

/* ═══════════════════════════════════════════════════
   tpo_apply()
   Time-Proportional Output — call once per TICK_MS.
   Converts heaterpower (0–100) to SSR on/off pattern
   over a TPO_PERIOD_MS window.
   ═══════════════════════════════════════════════════ */
void tpo_apply(void)
{
    static int tick = 0;

    int on_ticks = (int)((heaterpower / 100.0f) * TPO_TICKS + 0.5f);
    gpio_put(PIN_SSR, (tick < on_ticks) ? 1 : 0);
    if (++tick >= TPO_TICKS) tick = 0;

    // update PWM bargraph display
    float hp = heaterpower * (float)PWM_WRAP_VAL / 100.0;
    pwm_set_chan_level(PWM_SLICE_NUM, PWM_CHAN_A, (uint16_t)(hp));
}
