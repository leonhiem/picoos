/*
 * cue_icons.h - Two tiny static label icons, always drawn once and
 * never changing: a thermometer next to the baby's face, a clock next
 * to the APGAR timer. Purely to disambiguate "this row is about
 * temperature" from "this row is a clock reading" at a glance - see
 * art/render_cue_icons.py for the full rationale.
 *
 * Unlike icons.h's status icons, these have no on/off state - they're
 * plain labels, not indicators. They're also two different sizes (the
 * thermometer's capsule+bulb shape needs to be bigger to stay
 * recognizable than the clock's simpler circle+hands does) - callers
 * don't need to know either size, just where the top-left goes.
 */
#ifndef CUE_ICONS_H
#define CUE_ICONS_H

#include <stdint.h>

/* Draw at (x, y) = top-left. `bg` is unused (bitmap is full-bleed) but
 * kept for call-site symmetry with the other render_*() helpers. */
void cue_icon_thermometer_draw(int16_t x, int16_t y, uint16_t bg);
void cue_icon_clock_draw(int16_t x, int16_t y, uint16_t bg);

#endif /* CUE_ICONS_H */
