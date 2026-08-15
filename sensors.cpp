#include "warmer.h"
#include "hardware/adc.h"
#include "ntc_lut.h"
#include <cstdio>

// Owned by task_sensor; main() also touches it once for setup.
struct ads1115_adc adc;

static const char *state_str[] = {"idl","man","p1A","coa","pid","saf"};


void task_sensor(void)
{
    static int line_idx = 0;
    int i;

            // Read sensor task

            // Read heater current from ADC
            adc_select_input(0);
            uint16_t adc_result = adc_read();
            heatercheck.curr_sense_table[heatercheck.c_idx] = adc_result;
            heatercheck.c_idx++;
            heatercheck.c_idx&=(HEAT_table_LEN-1);
            heatercheck.curr_sense = 0;
            for(i=0;i<HEAT_table_LEN;i++) {
                heatercheck.curr_sense += heatercheck.curr_sense_table[i];
            }
            heatercheck.curr_sense = heatercheck.curr_sense / HEAT_table_LEN;

            // Read temperature sensors
            uint16_t adc_value;
            short adc_sample;
    


            ads1115_set_input_mux(ADS1115_MUX_DIFF_0_1, &adc);
            ads1115_write_config(&adc);
            ads1115_read_adc(&adc_value, &adc);
            adc_sample=(short)adc_value;
            adc_sample+=11025; // 0V -> 50degC
            adc_sample=adc_sample>>5;

            tempctl.temphigh = false;
            tempctl.templow = false;

            if(adc_sample<0) {
                adc_sample=0;
                tempctl.temphigh = true;
            }
            if(adc_sample>649) {
                adc_sample=649;
                tempctl.templow = true;
            }
            tempctl.t_skin = (float)(ntc_lut[adc_sample]);
            tempctl.t_skin /= 10.0;

            ads1115_set_input_mux(ADS1115_MUX_DIFF_2_3, &adc);
            ads1115_write_config(&adc);
            ads1115_read_adc(&adc_value, &adc);
            adc_sample=(short)adc_value;
            adc_sample+=11025; // 0V -> 50degC
            adc_sample=adc_sample>>5;

            if(adc_sample<0) {
                adc_sample=0;
            }
            if(adc_sample>649) {
                adc_sample=649;
            }
            tempctl.t_ambient = (float)(ntc_lut[adc_sample]);
            tempctl.t_ambient /= 10.0;


            if(tempctl.t_skin > (tempctl.setpoint.temp+2.0)) tempctl.temphigh = true;
            if(tempctl.t_skin < (tempctl.t_ambient-2.0)) tempctl.templow = true;


            tempctl.skin_ok = true;
            tempctl.amb_ok = true;
            display_needs_refresh=true;

            // Display telemetry
            // pid_i and pid_pm are only meaningful in [pid] state, but logged
            // unconditionally so columns line up across phases.
            if((line_idx % 10)==0) {
                printf("state setp skin amb  heat  low  high  curr fail warn pid_i  pid_pm\n");
            }
            line_idx++;
            printf("[%s] %.1f %.1f %.1f %.1f   %d     %d   %d   %d   %d   %+.2f  %.2f\n",
                   state_str[state],tempctl.setpoint.temp,
                   tempctl.t_skin,tempctl.t_ambient,tempctl.heaterpower,
                   tempctl.templow,tempctl.temphigh,heatercheck.curr_sense,
                   heatercheck.fail,safecheck.warn,
                   tempctl.pid.integral, tempctl.pid.prev_meas);
}
