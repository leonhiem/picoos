#include "warmer.h"
#include "hardware/pwm.h"
#include "kernel/task.h"
#include <cstdio>


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
void tpo_apply(void)
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

// Owned by task_pidctrl -- the only writer. task_sensor reads it
// (read-only) for its telemetry line.
ControlState state = STATE_IDLE;

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
