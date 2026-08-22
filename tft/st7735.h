/*
 * st7735.h - Low-level ST7735 driver for RP2040
 *
 * Hardware:
 *   spi0 at 32 MHz on GPIO 22 (SCK) and GPIO 23 (MOSI)
 *   CS, DC, RST, LED are software-controlled GPIOs
 *
 * Display: 1.8" 128x160 portrait
 *   x: 0..127 (width)
 *   y: 0..159 (height)
 *
 * Color format: RGB565
 */
#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- pin assignments (custom PCB) ---- */
#define ST7735_SPI_PORT     spi0
#define ST7735_SPI_BAUD     32000000u   /* 32 MHz */
#define ST7735_SCK_PIN      22
#define ST7735_MOSI_PIN     23
#define ST7735_CS_PIN       25
#define ST7735_DC_PIN       24          /* a.k.a. A0 */
#define ST7735_RST_PIN      21
#define ST7735_LED_PIN      20

/* ---- display geometry (portrait) ---- */
#define ST7735_WIDTH        128
#define ST7735_HEIGHT       160

/* ---- common RGB565 colors ---- */
#define COLOR_BLACK         0x0000
#define COLOR_WHITE         0xFFFF
#define COLOR_RED           0xF800
#define COLOR_DARK_RED      0x7800
#define COLOR_GREEN         0x07E0
#define COLOR_DARK_GREEN    0x03E0
#define COLOR_BLUE          0x001F
#define COLOR_DARK_BLUE     0x000F
#define COLOR_YELLOW        0xFFE0
#define COLOR_DARK_YELLOW   0x8400
#define COLOR_ORANGE        0xFC00
#define COLOR_DARK_ORANGE   0x7A00
#define COLOR_CYAN          0x07FF
#define COLOR_MAGENTA       0xF81F
#define COLOR_GREY          0x4208      /* dark grey for "off" icons */
#define COLOR_LIGHT_GREY    0x8410
#define COLOR_DIM_GREY      RGB565(32, 32, 32)  /* near-black: divider line,
                                                    matches the icon "off" grey */
#define COLOR_SKIN          0xFE19      /* warm peach skin tone */
#define COLOR_SKIN_COLD     0xDEFB      /* pale-blue skin */
#define COLOR_SKIN_HOT      0xFE16      /* flushed pink */

/* RGB888 -> RGB565 helper (compile-time) */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* ---- low-level API ---- */
void st7735_init(void);
void st7735_backlight(bool on);
void st7735_set_addr_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void st7735_write_cmd(uint8_t cmd);
void st7735_write_data(uint8_t d);
void st7735_write_data_buf(const uint8_t *buf, size_t len);

/* ---- pixel-level primitive (used by gfx.c) ---- */
void st7735_draw_pixel(int16_t x, int16_t y, uint16_t color);

/* ---- bulk fill (much faster than per-pixel) ---- */
void st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void st7735_fill_screen(uint16_t color);

/* ---- bitmap blit: draws a w*h RGB565 image (row-major) at (x, y).
 * `bitmap` is typically a `const` array stored in flash. */
void st7735_draw_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *bitmap);

#endif /* ST7735_H */
