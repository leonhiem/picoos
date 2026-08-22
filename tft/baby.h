/*
 * baby.h - Baby face (emoji), rendered from a pre-baked bitmap.
 *
 * Was a full swaddled-baby figure; replaced with just the 3 OpenMoji
 * emoji faces per direction ("go with the 3 emoji faces" / "remove the
 * baby icon"). BABY_WARMING has no distinct emoji of its own and reuses
 * the cold face until told otherwise - see baby.c.
 */
#ifndef BABY_H
#define BABY_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BABY_COLD     = 0,
    BABY_WARMING  = 1,
    BABY_OK       = 2,    /* comfortable, on target */
    BABY_HOT      = 3,
} baby_state_t;

/* Draw the face at (cx, cy) = center. `bg` is unused (bitmap is
 * full-bleed) but kept for call-site symmetry with the old API. */
void baby_draw(int16_t cx, int16_t cy, baby_state_t state, uint16_t bg);

#endif /* BABY_H */
