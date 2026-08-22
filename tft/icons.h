/*
 * icons.h - Five status row icons for the baby warmer display.
 *
 * Each icon fits in a 28x28 box and is drawn at (x, y) = top-left.
 * State controls visibility:
 *   ICON_OFF = dim/grey (inactive)
 *   ICON_ON  = bright (active - the caller toggles ON/OFF over time to
 *              blink, e.g. warning/alarm/sensor-problem)
 *
 * NOTE: each draw function fully repaints the 28x28 region (it's a full
 *       bitmap blit), so no stale pixels remain.
 */
#ifndef ICONS_H
#define ICONS_H

#include <stdint.h>
#include <stdbool.h>

#define ICON_W 28
#define ICON_H 28

typedef enum {
    ICON_OFF   = 0,
    ICON_ON    = 1,
} icon_state_t;

/* Each icon clears its 28x28 box to `bg` first */
void icon_mode_auto   (int16_t x, int16_t y, icon_state_t s, uint16_t bg);
void icon_mode_manual (int16_t x, int16_t y, icon_state_t s, uint16_t bg);
void icon_sensor      (int16_t x, int16_t y, icon_state_t s, uint16_t bg);
void icon_warning     (int16_t x, int16_t y, icon_state_t s, uint16_t bg);
void icon_alarm       (int16_t x, int16_t y, icon_state_t s, uint16_t bg);

#endif /* ICONS_H */
