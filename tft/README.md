# tft/ — vendored ST7735 display driver (blackbox)

Everything in this directory is copied, unmodified, from
`../../st7735/input/` (github.com/leonhiem/st7735) as of 2026-08-23.
It is not built or maintained here — treat it as a blackbox.

If the `st7735` repo's display gets improved (new icons, layout
changes, bugfixes), re-copy the same file list from there:

```
st7735.c st7735.h
gfx.c gfx.h
icons.c icons.h icon_bitmaps.h
baby.c baby.h face_bitmaps.h
heat_indicator.c heat_indicator.h
display.c display.h
```

picoos's own code (`dev/tft.cpp`, `prog/tftwire.cpp`) only ever
includes `display.h` and calls `display_init()` / `display_update()`
— never anything from `st7735.c`/`gfx.c`/etc directly. Keep it that
way: if picoos code ever needs to reach past `display.h`, that's a
sign the display.h API needs to grow, not that the blackbox boundary
should move.
