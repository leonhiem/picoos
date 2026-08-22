/*
 * baby.c - Baby face, rendered from pre-baked OpenMoji bitmaps.
 *
 * 3 real emoji (input/art/render_faces.py, OpenMoji CC BY-SA 4.0):
 *   BABY_OK   -> 1F60A smiling face with smiling eyes
 *   BABY_COLD -> 1F976 cold face
 *   BABY_HOT  -> 1F975 hot face
 * BABY_WARMING has no distinct emoji and reuses the cold face (baby
 * hasn't reached comfortable yet) - flag if a dedicated one is wanted.
 */
#include "baby.h"
#include "st7735.h"
#include "face_bitmaps.h"

#define FACE_W 80
#define FACE_H 80

void baby_draw(int16_t cx, int16_t cy, baby_state_t state, uint16_t bg) {
    (void)bg; /* bitmap is full-bleed; nothing to pre-clear */

    const uint16_t *bmp;
    switch (state) {
    case BABY_COLD:
    case BABY_WARMING: bmp = face_cold; break;
    case BABY_HOT:      bmp = face_hot;  break;
    case BABY_OK:
    default:            bmp = face_ok;   break;
    }

    st7735_draw_bitmap(cx - FACE_W / 2, cy - FACE_H / 2, FACE_W, FACE_H, bmp);
}
