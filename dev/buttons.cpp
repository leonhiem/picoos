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
 */
#include "warmer.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static const char *names[6] = {"up", "down", "mute", "manual", "start", "lamp"};

static int buttons_read(char *buf, int len)
{
    int pos = 0;
    bool any = false;

    for (int i = 0; i < 6 && pos < len; i++) {
        if (!button[i]) continue;
        button[i] = false; // consume the event
        any = true;
        int n = snprintf(buf + pos, len - pos, "%s ", names[i]);
        if (n < 0) break;
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
