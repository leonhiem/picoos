/**
 * dev/buttons.cpp — /dev/buttons: read-only, the front-panel buttons
 *
 * cat /dev/buttons -> the names of whichever buttons were pressed since
 * the last read, e.g. "up down\n", or "none\n" if nothing's happened.
 * Read-and-clear (event) semantics, not a live "is held down" snapshot:
 * button[] (buttons.cpp) is a sticky per-button flag set once by the
 * debounce timer callback on press and never cleared since task_input
 * (the old consumer) was ripped out -- this device is now that consumer.
 *
 * Note: button[] is written from the debounce timer's interrupt context
 * (buttons.cpp's repeating_timer_callback) while this read() runs from
 * the cooperative task_shell. No lock around the read-then-clear here --
 * worst case is a press landing in the few-microsecond window between
 * the two and getting missed on this particular cat. Fine for an
 * interactive button-status device; not fine if this ever backs
 * something that must never miss a press.
 *
 * See also /dev/buttons/<name> further down -- six independent per-
 * button siblings that don't have the all-or-nothing consumption
 * problem this aggregate device does.
 */
#include "warmer.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static const char *names[6] = {"up", "down", "mute", "manual", "start", "lamp"};

static int buttons_read(char *buf, int len)
{
    // snprintf()'s return value is how much it *would* write, not how
    // much it did -- must stop, not just count, once a write would
    // truncate (see prog/ls.cpp for the full explanation), and check
    // that *before* consuming the event, so a button whose name didn't
    // fit doesn't get silently cleared without being reported.
    int pos = 0;
    bool any = false;

    for (int i = 0; i < 6; i++) {
        if (!button[i]) continue;
        int avail = len - pos;
        int n = snprintf(buf + pos, avail, "%s ", names[i]);
        if (n < 0 || n >= avail) break;
        button[i] = false; // consume the event -- only now that it fit
        any = true;
        pos += n;
    }

    if (!any) {
        return snprintf(buf, len, "none\n");
    }
    if (pos > 0 && pos <= len) buf[pos - 1] = '\n'; // swap trailing space
    return pos;
}

static const device_t dev_buttons = {
    "/dev/buttons",
    0,             // no open needed -- debounce is already running
    0,             // no close needed
    buttons_read,
    0,             // read-only
};

void buttons_register(void)
{
    fs_register(&dev_buttons);
}

/* ═══════════════════════════════════════════════════
   /dev/buttons/<name> -- one independent read-and-clear device per
   button, alongside the aggregate /dev/buttons above. The aggregate's
   read() drains *every* pending button at once (see the header
   comment); a consumer watching just one button through it would
   silently steal the others' events too -- which is exactly why
   dev/setpoint.cpp had to bypass it and read button[] directly instead
   of going through a device. These fix that generally: reading one of
   these only ever touches its own button's flag.

   No way to pass a button index through device_t's plain read()
   function pointer, so these are hand-duplicated one per button --
   same tradeoff as jobs.cpp's job trampolines, same reasoning: fine at
   six, not worth generalizing for.
   ═══════════════════════════════════════════════════ */
static int one_read(int idx, char *buf, int len)
{
    bool pressed = button[idx];
    button[idx] = false;
    return snprintf(buf, len, pressed ? "1\n" : "0\n");
}

static int up_read(char *buf, int len)     { return one_read(BUTTON_UP, buf, len); }
static int down_read(char *buf, int len)   { return one_read(BUTTON_DOWN, buf, len); }
static int mute_read(char *buf, int len)   { return one_read(BUTTON_MUTE, buf, len); }
static int manual_read(char *buf, int len) { return one_read(BUTTON_MANUAL, buf, len); }
static int start_read(char *buf, int len)  { return one_read(BUTTON_START, buf, len); }
static int lamp_btn_read(char *buf, int len) { return one_read(BUTTON_LAMP, buf, len); }

static const device_t dev_button_up     = {"/dev/buttons/up",     0, 0, up_read,     0};
static const device_t dev_button_down   = {"/dev/buttons/down",   0, 0, down_read,   0};
static const device_t dev_button_mute   = {"/dev/buttons/mute",   0, 0, mute_read,   0};
static const device_t dev_button_manual = {"/dev/buttons/manual", 0, 0, manual_read, 0};
static const device_t dev_button_start  = {"/dev/buttons/start",  0, 0, start_read,  0};
static const device_t dev_button_lamp   = {"/dev/buttons/lamp",   0, 0, lamp_btn_read, 0};

void button_devices_register(void)
{
    fs_register(&dev_button_up);
    fs_register(&dev_button_down);
    fs_register(&dev_button_mute);
    fs_register(&dev_button_manual);
    fs_register(&dev_button_start);
    fs_register(&dev_button_lamp);
}
