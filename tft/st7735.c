/*
 * st7735.c - Low-level ST7735 driver
 */
#include "st7735.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

/* ---- ST7735 command set ---- */
#define CMD_SWRESET     0x01
#define CMD_SLPOUT      0x11
#define CMD_INVOFF      0x20
#define CMD_DISPON      0x29
#define CMD_CASET       0x2A
#define CMD_RASET       0x2B
#define CMD_RAMWR       0x2C
#define CMD_MADCTL      0x36
#define CMD_COLMOD      0x3A
#define CMD_FRMCTR1     0xB1
#define CMD_FRMCTR2     0xB2
#define CMD_FRMCTR3     0xB3
#define CMD_INVCTR      0xB4
#define CMD_PWCTR1      0xC0
#define CMD_PWCTR2      0xC1
#define CMD_PWCTR3      0xC2
#define CMD_PWCTR4      0xC3
#define CMD_PWCTR5      0xC4
#define CMD_VMCTR1      0xC5
#define CMD_GMCTRP1     0xE0
#define CMD_GMCTRN1     0xE1

/* ---- low-level SPI / GPIO helpers ---- */

static inline void cs_low(void)  { gpio_put(ST7735_CS_PIN, 0); }
static inline void cs_high(void) { gpio_put(ST7735_CS_PIN, 1); }
static inline void dc_cmd(void)  { gpio_put(ST7735_DC_PIN, 0); }
static inline void dc_data(void) { gpio_put(ST7735_DC_PIN, 1); }

void st7735_write_cmd(uint8_t cmd) {
    dc_cmd();
    cs_low();
    spi_write_blocking(ST7735_SPI_PORT, &cmd, 1);
    cs_high();
}

void st7735_write_data(uint8_t d) {
    dc_data();
    cs_low();
    spi_write_blocking(ST7735_SPI_PORT, &d, 1);
    cs_high();
}

void st7735_write_data_buf(const uint8_t *buf, size_t len) {
    dc_data();
    cs_low();
    spi_write_blocking(ST7735_SPI_PORT, buf, len);
    cs_high();
}

void st7735_set_addr_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    /* CASET: column address set */
    st7735_write_cmd(CMD_CASET);
    uint8_t col[4] = { 0x00, x0, 0x00, x1 };
    st7735_write_data_buf(col, 4);

    /* RASET: row address set */
    st7735_write_cmd(CMD_RASET);
    uint8_t row[4] = { 0x00, y0, 0x00, y1 };
    st7735_write_data_buf(row, 4);

    /* RAMWR: prepare to receive pixel data */
    st7735_write_cmd(CMD_RAMWR);
}

/* ---- hardware reset pulse ---- */
static void st7735_hw_reset(void) {
    gpio_put(ST7735_RST_PIN, 1);
    sleep_ms(5);
    gpio_put(ST7735_RST_PIN, 0);
    sleep_ms(20);
    gpio_put(ST7735_RST_PIN, 1);
    sleep_ms(150);
}

void st7735_backlight(bool on) {
    gpio_put(ST7735_LED_PIN, on ? 1 : 0);
}

/* ---- single-pixel draw ---- */
void st7735_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;

    st7735_set_addr_window((uint8_t)x, (uint8_t)y, (uint8_t)x, (uint8_t)y);

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    uint8_t px[2] = { hi, lo };

    dc_data();
    cs_low();
    spi_write_blocking(ST7735_SPI_PORT, px, 2);
    cs_high();
}

/* ---- bulk rectangle fill ----
 * Sets the address window once, then streams pixel bytes.
 * Uses a small stack buffer to send 32 pixels per SPI call,
 * which is much faster than 1 pixel at a time.
 */
void st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    /* clip to display bounds */
    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if (x + w > ST7735_WIDTH)  w = ST7735_WIDTH  - x;
    if (y + h > ST7735_HEIGHT) h = ST7735_HEIGHT - y;

    st7735_set_addr_window((uint8_t)x, (uint8_t)y, (uint8_t)(x + w - 1), (uint8_t)(y + h - 1));

    /* prepare a small chunk of identical pixels */
    enum { CHUNK = 32 };
    uint8_t buf[CHUNK * 2];
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    for (int i = 0; i < CHUNK; i++) {
        buf[2 * i + 0] = hi;
        buf[2 * i + 1] = lo;
    }

    uint32_t total = (uint32_t)w * (uint32_t)h;

    dc_data();
    cs_low();
    while (total >= CHUNK) {
        spi_write_blocking(ST7735_SPI_PORT, buf, sizeof(buf));
        total -= CHUNK;
    }
    if (total > 0) {
        spi_write_blocking(ST7735_SPI_PORT, buf, total * 2);
    }
    cs_high();
}

void st7735_fill_screen(uint16_t color) {
    st7735_fill_rect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

/* ---- bitmap blit ----
 * Same address-window + streaming approach as fill_rect, but each pixel
 * comes from `bitmap` instead of being constant. No clipping (unlike
 * fill_rect/draw_pixel) - callers are expected to place bitmaps fully
 * on-screen, since off-screen clipping would require skipping source
 * pixels too, which the addr-window approach can't do mid-stream.
 */
void st7735_draw_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *bitmap) {
    if (w <= 0 || h <= 0) return;
    if (x < 0 || y < 0 || x + w > ST7735_WIDTH || y + h > ST7735_HEIGHT) return;

    st7735_set_addr_window((uint8_t)x, (uint8_t)y, (uint8_t)(x + w - 1), (uint8_t)(y + h - 1));

    enum { CHUNK = 32 };
    uint8_t buf[CHUNK * 2];
    uint32_t total = (uint32_t)w * (uint32_t)h;
    uint32_t idx = 0;

    dc_data();
    cs_low();
    while (idx < total) {
        uint32_t n = total - idx;
        if (n > CHUNK) n = CHUNK;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t color = bitmap[idx + i];
            buf[2 * i + 0] = (uint8_t)(color >> 8);
            buf[2 * i + 1] = (uint8_t)(color & 0xFF);
        }
        spi_write_blocking(ST7735_SPI_PORT, buf, n * 2);
        idx += n;
    }
    cs_high();
}

/* ---- init sequence ---- */
void st7735_init(void) {
    /* SPI setup */
    spi_init(ST7735_SPI_PORT, ST7735_SPI_BAUD);
    gpio_set_function(ST7735_SCK_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(ST7735_MOSI_PIN, GPIO_FUNC_SPI);

    /* control GPIOs */
    gpio_init(ST7735_CS_PIN);
    gpio_set_dir(ST7735_CS_PIN, GPIO_OUT);
    gpio_put(ST7735_CS_PIN, 1);

    gpio_init(ST7735_DC_PIN);
    gpio_set_dir(ST7735_DC_PIN, GPIO_OUT);
    gpio_put(ST7735_DC_PIN, 1);

    gpio_init(ST7735_RST_PIN);
    gpio_set_dir(ST7735_RST_PIN, GPIO_OUT);
    gpio_put(ST7735_RST_PIN, 1);

    gpio_init(ST7735_LED_PIN);
    gpio_set_dir(ST7735_LED_PIN, GPIO_OUT);
    gpio_put(ST7735_LED_PIN, 1);   /* backlight on */

    /* hardware reset pulse */
    st7735_hw_reset();

    /* software reset + sleep out */
    st7735_write_cmd(CMD_SWRESET);
    sleep_ms(150);
    st7735_write_cmd(CMD_SLPOUT);
    sleep_ms(120);

    /* frame rate (default-ish) */
    st7735_write_cmd(CMD_FRMCTR1);
    {
        uint8_t d[3] = { 0x01, 0x2C, 0x2D };
        st7735_write_data_buf(d, 3);
    }
    st7735_write_cmd(CMD_FRMCTR2);
    {
        uint8_t d[3] = { 0x01, 0x2C, 0x2D };
        st7735_write_data_buf(d, 3);
    }
    st7735_write_cmd(CMD_FRMCTR3);
    {
        uint8_t d[6] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D };
        st7735_write_data_buf(d, 6);
    }

    /* inversion off */
    st7735_write_cmd(CMD_INVCTR);
    st7735_write_data(0x07);

    /* power control */
    st7735_write_cmd(CMD_PWCTR1);
    {
        uint8_t d[3] = { 0xA2, 0x02, 0x84 };
        st7735_write_data_buf(d, 3);
    }
    st7735_write_cmd(CMD_PWCTR2);
    st7735_write_data(0xC5);
    st7735_write_cmd(CMD_PWCTR3);
    {
        uint8_t d[2] = { 0x0A, 0x00 };
        st7735_write_data_buf(d, 2);
    }
    st7735_write_cmd(CMD_PWCTR4);
    {
        uint8_t d[2] = { 0x8A, 0x2A };
        st7735_write_data_buf(d, 2);
    }
    st7735_write_cmd(CMD_PWCTR5);
    {
        uint8_t d[2] = { 0x8A, 0xEE };
        st7735_write_data_buf(d, 2);
    }

    st7735_write_cmd(CMD_VMCTR1);
    st7735_write_data(0x0E);

    st7735_write_cmd(CMD_INVOFF);

    /* memory access control: portrait, RGB order */
    st7735_write_cmd(CMD_MADCTL);
    st7735_write_data(0xC0);   /* MY=1, MX=1, MV=0, RGB. Adjust to 0x00/0x08 if colors/orientation wrong */

    /* 16-bit color */
    st7735_write_cmd(CMD_COLMOD);
    st7735_write_data(0x05);

    /* gamma curves (typical defaults) */
    st7735_write_cmd(CMD_GMCTRP1);
    {
        uint8_t d[16] = {
            0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
            0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10
        };
        st7735_write_data_buf(d, 16);
    }
    st7735_write_cmd(CMD_GMCTRN1);
    {
        uint8_t d[16] = {
            0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
            0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10
        };
        st7735_write_data_buf(d, 16);
    }

    /* display on */
    st7735_write_cmd(CMD_DISPON);
    sleep_ms(100);

    /* clear */
    st7735_fill_screen(COLOR_BLACK);
}
