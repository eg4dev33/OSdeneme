// OSdeneme v0.4 - Grafik cizim islemleri
#include "gfx.hpp"

#include "memory.hpp"

extern const uint8_t FONT8X16[128][16];

namespace {

uint8_t* g_fb = nullptr;
uint32_t g_pitch = 0;
int g_w = 0;
int g_h = 0;
int g_bpp = 32;
uint32_t* g_back = nullptr;  // arka tampon: g_w * g_h piksel
uint8_t g_rs = 16, g_gs = 8, g_bs = 0;
bool g_std = true;  // 0x00RRGGBB duzeni
int g_cx0 = 0, g_cy0 = 0, g_cx1 = 0, g_cy1 = 0;  // kirpma: [x0,x1) x [y0,y1)

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }

inline uint32_t pack(uint32_t rgb) {
    if (g_std) return rgb;
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (r << g_rs) | (g << g_gs) | (b << g_bs);
}

inline void fill32(uint32_t* p, uint32_t v, size_t n) {
#if defined(__x86_64__)
    asm volatile("rep stosl" : "+D"(p), "+c"(n) : "a"(v) : "memory");
#else
    while (n--) *p++ = v;
#endif
}

inline void copy32(void* dst, const void* src, size_t n) {
#if defined(__x86_64__)
    asm volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
#else
    uint32_t* d = static_cast<uint32_t*>(dst);
    const uint32_t* s = static_cast<const uint32_t*>(src);
    while (n--) *d++ = *s++;
#endif
}

inline void px(int x, int y, uint32_t native) {
    if (x < g_cx0 || x >= g_cx1 || y < g_cy0 || y >= g_cy1) return;
    g_back[y * g_w + x] = native;
}

}  // namespace

bool gfx_attach(void* framebuffer, uint32_t pitch_bytes, int width, int height, int bpp,
                uint8_t red_shift, uint8_t green_shift, uint8_t blue_shift) {
    if (width <= 0 || height <= 0 || (bpp != 32 && bpp != 24)) return false;
    g_back = static_cast<uint32_t*>(kmalloc(static_cast<size_t>(width) * height * 4));
    if (g_back == nullptr) return false;
    g_fb = static_cast<uint8_t*>(framebuffer);
    g_pitch = pitch_bytes;
    g_w = width;
    g_h = height;
    g_bpp = bpp;
    g_rs = red_shift;
    g_gs = green_shift;
    g_bs = blue_shift;
    g_std = (g_rs == 16 && g_gs == 8 && g_bs == 0);
    fill32(g_back, 0, static_cast<size_t>(g_w) * g_h);
    gfx_reset_clip();
    return true;
}

bool gfx_ready() { return g_back != nullptr; }
int gfx_width() { return g_w; }
int gfx_height() { return g_h; }

void gfx_flip() {
    if (!g_back) return;
    for (int y = 0; y < g_h; y++) {
        const uint32_t* src = &g_back[y * g_w];
        uint8_t* dst = g_fb + static_cast<size_t>(y) * g_pitch;
        if (g_bpp == 32) {
            copy32(dst, src, static_cast<size_t>(g_w));
        } else {
            for (int x = 0; x < g_w; x++) {
                uint32_t c = src[x];
                dst[x * 3 + 0] = static_cast<uint8_t>(c);
                dst[x * 3 + 1] = static_cast<uint8_t>(c >> 8);
                dst[x * 3 + 2] = static_cast<uint8_t>(c >> 16);
            }
        }
    }
}

void gfx_set_clip(int x, int y, int w, int h) {
    g_cx0 = imax(x, 0);
    g_cy0 = imax(y, 0);
    g_cx1 = imin(x + w, g_w);
    g_cy1 = imin(y + h, g_h);
}

void gfx_reset_clip() {
    g_cx0 = 0;
    g_cy0 = 0;
    g_cx1 = g_w;
    g_cy1 = g_h;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (!g_back) return;
    int x0 = imax(x, g_cx0), y0 = imax(y, g_cy0);
    int x1 = imin(x + w, g_cx1), y1 = imin(y + h, g_cy1);
    if (x1 <= x0 || y1 <= y0) return;
    uint32_t c = pack(rgb);
    for (int yy = y0; yy < y1; yy++) fill32(&g_back[yy * g_w + x0], c, static_cast<size_t>(x1 - x0));
}

void gfx_blend_rect(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha) {
    if (!g_back) return;
    int x0 = imax(x, g_cx0), y0 = imax(y, g_cy0);
    int x1 = imin(x + w, g_cx1), y1 = imin(y + h, g_cy1);
    if (x1 <= x0 || y1 <= y0) return;
    uint32_t sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    uint32_t a = alpha, ia = 255 - alpha;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* p = &g_back[yy * g_w + x0];
        for (int xx = x0; xx < x1; xx++, p++) {
            uint32_t d = *p;
            uint32_t dr = (d >> g_rs) & 0xFF, dg = (d >> g_gs) & 0xFF, db = (d >> g_bs) & 0xFF;
            uint32_t r = (sr * a + dr * ia) / 255;
            uint32_t g = (sg * a + dg * ia) / 255;
            uint32_t b = (sb * a + db * ia) / 255;
            *p = (r << g_rs) | (g << g_gs) | (b << g_bs);
        }
    }
}

void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (h <= 0) return;
    int tr = (top >> 16) & 0xFF, tg = (top >> 8) & 0xFF, tb = top & 0xFF;
    int br = (bottom >> 16) & 0xFF, bg = (bottom >> 8) & 0xFF, bb = bottom & 0xFF;
    for (int i = 0; i < h; i++) {
        int den = h > 1 ? h - 1 : 1;
        int r = tr + (br - tr) * i / den;
        int g = tg + (bg - tg) * i / den;
        int b = tb + (bb - tb) * i / den;
        gfx_fill_rect(x, y + i, w, 1, (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b));
    }
}

void gfx_rect_outline(int x, int y, int w, int h, uint32_t rgb) {
    gfx_fill_rect(x, y, w, 1, rgb);
    gfx_fill_rect(x, y + h - 1, w, 1, rgb);
    gfx_fill_rect(x, y, 1, h, rgb);
    gfx_fill_rect(x + w - 1, y, 1, h, rgb);
}

void gfx_put_pixel(int x, int y, uint32_t rgb) {
    if (!g_back) return;
    px(x, y, pack(rgb));
}

void gfx_draw_char(int x, int y, char c, uint32_t rgb, int scale) {
    if (!g_back) return;
    unsigned idx = static_cast<unsigned char>(c);
    if (idx >= 128) idx = '?';
    uint32_t native = pack(rgb);
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = FONT8X16[idx][row];
        if (!bits) continue;
        for (int col = 0; col < FONT_W; col++) {
            if (!(bits & (0x80 >> col))) continue;
            if (scale == 1) {
                px(x + col, y + row, native);
            } else {
                gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, rgb);
            }
        }
    }
}

void gfx_draw_text(int x, int y, const char* s, uint32_t rgb, int scale) {
    for (; *s; s++) {
        gfx_draw_char(x, y, *s, rgb, scale);
        x += FONT_W * scale;
    }
}

int gfx_text_width(const char* s, int scale) {
    int n = 0;
    while (s[n]) n++;
    return n * FONT_W * scale;
}
