# tft/ — vendored ST7735 display driver (blackbox)

Everything in this directory is copied, unmodified, from
`../../st7735/input/` (github.com/leonhiem/st7735) — last re-synced
2026-08-28 (APGAR clock). It is not built or maintained here — treat
it as a blackbox.

If the `st7735` repo's display gets improved (new icons, layout
changes, bugfixes), re-copy the same file list from there:

```
st7735.c st7735.h
gfx.c gfx.h
icons.c icons.h icon_bitmaps.h
baby.c baby.h face_bitmaps.h
heat_indicator.c heat_indicator.h
apgar_timer.c apgar_timer.h digit_bitmaps.h
display.c display.h
```

Do NOT define `APGAR_FAST_DEMO` in picoos's build — that macro
(apgar_timer.c) shrinks the 1/5/10-minute checkpoints to seconds for
bench preview only; picoos's CMakeLists.txt doesn't set it, so
apgar_timer.c falls through to its `#ifndef` branch (real minutes) by
default. Leave it that way.

picoos's own code (`dev/tft.cpp`, `prog/tftwire.cpp`) only ever
includes `display.h` and calls `display_init()` / `display_update()`
— never anything from `st7735.c`/`gfx.c`/etc directly. Keep it that
way: if picoos code ever needs to reach past `display.h`, that's a
sign the display.h API needs to grow, not that the blackbox boundary
should move.
