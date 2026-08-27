/*
 * apgar_timer.h - APGAR elapsed-time readout (MM:SS), pre-baked digit font.
 *
 * The APGAR score is normally read at fixed minutes after birth; showing
 * a running MM:SS clock next to the baby's face lets staff see at a
 * glance when the next check is due, without a wall clock.
 */
#ifndef APGAR_TIMER_H
#define APGAR_TIMER_H

#include <stdint.h>
#include <stdbool.h>

/* Draw "MM:SS" centered on cx, top edge at y. Caps display at 99:59.
 * `bg` is unused (glyph bitmaps are full-bleed) but kept for call-site
 * symmetry with the other render_*() helpers.
 *
 * `highlight` selects the yellow "attention" glyph set instead of the
 * normal white one - the caller (display.c) turns this on/off in sync
 * with the shared blink timer to flash the readout around the standard
 * APGAR checkpoints (1 min / 5 min / 10 min after birth). See
 * apgar_timer_in_checkpoint_window(). */
void apgar_timer_draw(int16_t cx, int16_t y, uint32_t total_seconds,
                       bool highlight, uint16_t bg);

/* True while `total_seconds` is within the flash window of a standard
 * APGAR checkpoint (1 min, 5 min, 10 min). Caller ANDs this with its own
 * blink phase to make the readout flash rather than stay solid yellow. */
bool apgar_timer_in_checkpoint_window(uint32_t total_seconds);

#endif /* APGAR_TIMER_H */
