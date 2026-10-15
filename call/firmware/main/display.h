#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH  296
#define DISPLAY_HEIGHT 240

// RGB565 colours.
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_GREY   0x8410
#define COLOR_GREEN  0x2E8B
#define COLOR_RED    0xE8E4
#define COLOR_ORANGE 0xFC60

typedef enum {
    DISPLAY_FONT_SMALL,  // 20 px
    DISPLAY_FONT_LARGE,  // 40 px
} display_font_t;

void display_init(void);

// Drawing goes to a framebuffer; display_flush() pushes it to the panel.
void display_fill(uint16_t color);
void display_fill_circle(int cx, int cy, int r, uint16_t color);
void display_text(int x, int y, const char *text, display_font_t size, uint16_t color);
void display_text_centered(int y, const char *text, display_font_t size, uint16_t color);
int display_text_width(const char *text, display_font_t size);
void display_flush(void);

#ifdef __cplusplus
}
#endif
