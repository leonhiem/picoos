/*
 * heat_indicator.h - Heater "resistor" (heating element) + radiating heat
 * rays, drawn above the baby's face.
 *
 * The element is drawn like a resistor: two wire leads plus a capsule
 * body. Its fill color, and the color of all 5 heat rays below it, come
 * from the same 6-stop palette keyed by heater percent:
 *
 *   0%   grey       (matches the icon "off" grey - idle)
 *   20%  light grey
 *   40%  dark red
 *   60%  red
 *   80%  dark orange
 *   100% orange
 *
 * All 5 rays are always drawn (not hidden at low percent) - only their
 * color changes. `heater_failed` forces the element solid red with all
 * rays off, regardless of percent (a fault, not genuine heat).
 */
#ifndef HEAT_INDICATOR_H
#define HEAT_INDICATOR_H

#include <stdint.h>
#include <stdbool.h>

/* Redraws the whole element+rays region (bounding box, so partial
 * states never leave stale pixels). Call whenever percent/heater_failed
 * change.
 *
 *   elem_cx, elem_y  - element top-left is (elem_cx - elem_w/2, elem_y)
 *   elem_w, elem_h   - element bounding size (leads + body)
 *   rays_top_y       - where the rays start (just below the element)
 *   rays_bot_y       - where the rays end (just above the face)
 */
void heat_indicator_update(int16_t elem_cx, int16_t elem_y, int16_t elem_w, int16_t elem_h,
                            int16_t rays_top_y, int16_t rays_bot_y,
                            uint8_t percent, bool heater_failed, uint16_t bg);

#endif /* HEAT_INDICATOR_H */
