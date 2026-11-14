// OSdeneme v0.4 - Grafik: arka tampon + temel cizim islemleri. Renkler 0xRRGGBB.
#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr int FONT_W = 8;
constexpr int FONT_H = 16;

// Ekrani (framebuffer) ve arka tamponu baglar. bpp: 24 ya da 32.
bool gfx_attach(void* framebuffer, uint32_t pitch_bytes, int width, int height, int bpp,
                uint8_t red_shift, uint8_t green_shift, uint8_t blue_shift);
bool gfx_ready();
int gfx_width();
int gfx_height();
void gfx_flip();  // arka tamponu ekrana kopyalar

void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip();

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_blend_rect(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_rect_outline(int x, int y, int w, int h, uint32_t rgb);
void gfx_put_pixel(int x, int y, uint32_t rgb);
void gfx_draw_char(int x, int y, char c, uint32_t rgb, int scale = 1);
void gfx_draw_text(int x, int y, const char* s, uint32_t rgb, int scale = 1);
int gfx_text_width(const char* s, int scale = 1);
