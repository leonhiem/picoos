#include "warmer.h"
#include "hardware/gpio.h"
#include <cstdio>

extern const uint  button_pin[6] = { BUTTON_PIN_UP,
                              BUTTON_PIN_DOWN,
                              BUTTON_PIN_MUTE,
                              BUTTON_PIN_MANUAL,
                              BUTTON_PIN_START,
                              BUTTON_PIN_LAMP };

// button irq begin
volatile bool button_state;
volatile bool button_pressed[6];
volatile bool button[6];
volatile int  button_cnt[6];
//volatile int  any_button_pressed_delay;

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
