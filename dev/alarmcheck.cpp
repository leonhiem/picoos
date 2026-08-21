/**
 * dev/alarmcheck.cpp — /dev/alarm/{heater,temphigh,templow}
 *
 * The three conditions prog/alarmcheck.cpp detects, bundled in one
 * file the same reason dev/pid.cpp/dev/phase.cpp bundle their own
 * paired program's devices. Together these are what defines "the
 * alarm should be on" -- reacting to that (buzzer/LED, mute) is a
 * separate program, prog/alarm.cpp, which ORs these three together.
 *
 * Each is read/write, "on"/"off", same shape as every other boolean
 * device in this namespace -- nothing stops a human writing one
 * directly, harmless (prog/alarmcheck.cpp will overwrite it on its own
 * next 15s check).
 *
 * heater: current-sense/commanded-power mismatch (was /dev/heaterfail,
 * renamed once temphigh/templow joined it under the same /dev/alarm/*
 * umbrella -- "heaterfail, temphigh, and templow define the alarm").
 * temphigh/templow: skin temp outside a fixed range. Leon's own fixed
 * testing values (40°C / 10°C) rather than babywarmer's original
 * relative-to-setpoint/relative-to-ambient margins -- see
 * prog/alarmcheck.cpp's header comment for why.
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static bool heater_fail   = false;
static bool temphigh_fail = false;
static bool templow_fail  = false;

static int read_bool(char *buf, int len, bool v)  { return snprintf(buf, len, v ? "on\n" : "off\n"); }

static int write_bool(const char *buf, int len, bool *dst)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { *dst = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { *dst = false; return len; }
    return -1;
}

static int heater_read(char *buf, int len)          { return read_bool(buf, len, heater_fail); }
static int heater_write(const char *buf, int len)   { return write_bool(buf, len, &heater_fail); }

static int temphigh_read(char *buf, int len)        { return read_bool(buf, len, temphigh_fail); }
static int temphigh_write(const char *buf, int len) { return write_bool(buf, len, &temphigh_fail); }

static int templow_read(char *buf, int len)         { return read_bool(buf, len, templow_fail); }
static int templow_write(const char *buf, int len)  { return write_bool(buf, len, &templow_fail); }

static const device_t dev_alarm_heater   = { "/dev/alarm/heater",   0, 0, heater_read,   heater_write };
static const device_t dev_alarm_temphigh = { "/dev/alarm/temphigh", 0, 0, temphigh_read, temphigh_write };
static const device_t dev_alarm_templow  = { "/dev/alarm/templow",  0, 0, templow_read,  templow_write };

void alarmcheck_devices_register(void)
{
    fs_register(&dev_alarm_heater);
    fs_register(&dev_alarm_temphigh);
    fs_register(&dev_alarm_templow);
}
