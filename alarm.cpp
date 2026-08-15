#include "warmer.h"
#include "hardware/gpio.h"

void task_alarm(void)
{
    //if(safecheck.warn || tempctl.templow || tempctl.temphigh || heatercheck.fail) {
    if( tempctl.templow || tempctl.temphigh || heatercheck.fail) {
        alarm.alarm = true;
    } else {
        alarm.alarm = false;
    }

        if(alarm.alarm) {
            gpio_put(PIN_ALARM_LED, 0); // PIN_ALARM_LED is inverted in hardware

            if(absolute_time_diff_us(get_absolute_time(),alarm.muted_time) < 0) {
                alarm.muted =false;
            }

            if(alarm.muted) {
                gpio_put(PIN_ALARM, 0);
            } else {
                gpio_put(PIN_ALARM, 1);
            }
        } else {
            gpio_put(PIN_ALARM_LED, 1);
            gpio_put(PIN_ALARM, 0);
            alarm.muted=false;
        }
}
