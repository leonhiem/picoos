/**
 * buttons.cpp — button debounce primitives only
 *
 * task_input (button-to-setpoint-editing logic) is gone with the
 * control loop it edited. What's left is the raw debounced button
 * state -- the natural thing for a future /dev/button device to read.
 */
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
