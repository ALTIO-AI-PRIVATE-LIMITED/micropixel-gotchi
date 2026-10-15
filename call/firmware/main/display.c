// JD9853 2.01" panel on the CGotchi, driven landscape (296x240) from a PSRAM
// framebuffer. The init sequence, orientation and 24-line gap are the ones
// the MicroPixel port uses (platform/boards/cheeko-gotchi/jd9853_panel.cpp).

#include "display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "font.h"

static const char *TAG = "display";

#define LCD_HOST      SPI2_HOST
#define LCD_SCLK      GPIO_NUM_9
#define LCD_MOSI      GPIO_NUM_10
#define LCD_CS        GPIO_NUM_14
#define LCD_DC        GPIO_NUM_8
#define LCD_RST       GPIO_NUM_17  // Also resets the CST810 touch controller.
#define LCD_BACKLIGHT GPIO_NUM_13  // Active high.

#define LCD_CLOCK_HZ  (40 * 1000 * 1000)
#define LCD_COL_GAP   24
#define STRIP_LINES   24

// MADCTL: MY (mirror rows) | MV (swap axes) | BGR.
#define LCD_MADCTL    (0x80 | 0x20 | 0x08)

typedef struct {
    uint8_t cmd;
    uint8_t data[32];
    uint8_t size;
} init_cmd_t;

// Panel maker's sequence for the BOE WV020JAU-N80, as the factory test runs it.
static const init_cmd_t init_cmds[] = {
    {0xDF, {0x98, 0x53}, 2},
    {0xDE, {0x00}, 1},
    {0xB2, {0x25}, 1},
    {0xB7, {0x00, 0x29, 0x00, 0x51}, 4},
    {0xBB, {0x4F, 0x1A, 0x55, 0x73, 0x63, 0xF0}, 6},
    {0xC0, {0x44, 0xA4}, 2},
    {0xC1, {0x12}, 1},
    {0xC3, {0x7D, 0x07, 0x14, 0x06, 0xC8, 0x71, 0x6C, 0x77}, 8},
    {0xC4, {0x00, 0x00, 0x94, 0x79, 0x25, 0x0A, 0x16, 0x79, 0x25, 0x0A, 0x16, 0x82}, 12},
    {0xC8, {0x3F, 0x34, 0x2D, 0x26, 0x2B, 0x2B, 0x25, 0x24, 0x23, 0x22, 0x20, 0x17, 0x14, 0x0E, 0x06, 0x00,
            0x3F, 0x34, 0x2D, 0x26, 0x2B, 0x2B, 0x25, 0x24, 0x23, 0x22, 0x20, 0x17, 0x14, 0x0E, 0x06, 0x00}, 32},
    {0xD0, {0x04, 0x06, 0x6B, 0x0F, 0x00}, 5},
    {0xD7, {0x00, 0x30}, 2},
    {0xE6, {0x10}, 1},
    {0xDE, {0x01}, 1},
    {0xB7, {0x03, 0x13, 0xEF, 0x35, 0x35}, 5},
    {0xC1, {0x14, 0x15, 0xC0}, 3},
    {0xC2, {0x06, 0x3A}, 2},
    {0xC4, {0x72, 0x12}, 2},
    {0xBE, {0x00}, 1},
    {0xDE, {0x00}, 1},
    {LCD_CMD_TEON, {0x00}, 1},
    {LCD_CMD_COLMOD, {0x05}, 1},  // RGB565
    {LCD_CMD_MADCTL, {LCD_MADCTL}, 1},
};

static esp_lcd_panel_io_handle_t io;
static uint16_t *framebuffer;  // PSRAM, pixels stored byte-swapped for SPI.
static uint16_t *strip;        // Internal DMA-capable copy buffer.
static SemaphoreHandle_t strip_done;

static bool on_color_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(strip_done, &woken);
    return woken == pdTRUE;
}

void display_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = DISPLAY_WIDTH * STRIP_LINES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    strip_done = xSemaphoreCreateBinary();
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_CLOCK_HZ,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = on_color_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << LCD_RST) | (1ULL << LCD_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_set_level(LCD_BACKLIGHT, 0);
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].size);
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    framebuffer = heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    strip = heap_caps_malloc(DISPLAY_WIDTH * STRIP_LINES * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(framebuffer && strip);

    display_fill(0x0000);
    display_flush();
    esp_lcd_panel_io_tx_param(io, LCD_CMD_DISPON, NULL, 0);
    gpio_set_level(LCD_BACKLIGHT, 1);
    ESP_LOGI(TAG, "JD9853 %dx%d ready", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

static inline uint16_t swap16(uint16_t c)
{
    return (uint16_t)((c << 8) | (c >> 8));
}

void display_fill(uint16_t color)
{
    uint16_t c = swap16(color);
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        framebuffer[i] = c;
    }
}

void display_fill_circle(int cx, int cy, int r, uint16_t color)
{
    uint16_t c = swap16(color);
    for (int y = -r; y <= r; y++) {
        int py = cy + y;
        if (py < 0 || py >= DISPLAY_HEIGHT) {
            continue;
        }
        for (int x = -r; x <= r; x++) {
            int px = cx + x;
            if (px >= 0 && px < DISPLAY_WIDTH && x * x + y * y <= r * r) {
                framebuffer[py * DISPLAY_WIDTH + px] = c;
            }
        }
    }
}

static const font_t *pick_font(display_font_t size)
{
    return size == DISPLAY_FONT_LARGE ? &font_large : &font_small;
}

int display_text_width(const char *text, display_font_t size)
{
    const font_t *font = pick_font(size);
    int width = 0;
    for (const char *p = text; *p; p++) {
        if (*p >= FONT_FIRST_CHAR && *p <= FONT_LAST_CHAR) {
            width += font->widths[*p - FONT_FIRST_CHAR];
        }
    }
    return width;
}

void display_text(int x, int y, const char *text, display_font_t size, uint16_t color)
{
    const font_t *font = pick_font(size);
    uint16_t c = swap16(color);
    for (const char *p = text; *p; p++) {
        if (*p < FONT_FIRST_CHAR || *p > FONT_LAST_CHAR) {
            continue;
        }
        int index = *p - FONT_FIRST_CHAR;
        int width = font->widths[index];
        int row_bytes = (width + 7) / 8;
        const uint8_t *glyph = font->bits + font->offsets[index];
        for (int gy = 0; gy < font->height; gy++) {
            int py = y + gy;
            if (py < 0 || py >= DISPLAY_HEIGHT) {
                continue;
            }
            for (int gx = 0; gx < width; gx++) {
                int px = x + gx;
                if (px >= 0 && px < DISPLAY_WIDTH && (glyph[gy * row_bytes + gx / 8] & (0x80 >> (gx % 8)))) {
                    framebuffer[py * DISPLAY_WIDTH + px] = c;
                }
            }
        }
        x += width;
    }
}

void display_text_centered(int y, const char *text, display_font_t size, uint16_t color)
{
    display_text((DISPLAY_WIDTH - display_text_width(text, size)) / 2, y, text, size, color);
}

void display_flush(void)
{
    const int x0 = LCD_COL_GAP;
    const int x1 = LCD_COL_GAP + DISPLAY_WIDTH - 1;
    for (int y = 0; y < DISPLAY_HEIGHT; y += STRIP_LINES) {
        int lines = DISPLAY_HEIGHT - y < STRIP_LINES ? DISPLAY_HEIGHT - y : STRIP_LINES;
        memcpy(strip, framebuffer + y * DISPLAY_WIDTH, DISPLAY_WIDTH * lines * 2);
        uint8_t cols[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
        uint8_t rows[] = {y >> 8, y & 0xFF, (y + lines - 1) >> 8, (y + lines - 1) & 0xFF};
        esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, cols, sizeof(cols));
        esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, rows, sizeof(rows));
        esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, strip, DISPLAY_WIDTH * lines * 2);
        xSemaphoreTake(strip_done, portMAX_DELAY);
    }
}
