// OSdeneme v0.4 - Masaustu
#include "desktop.hpp"

#include "gfx.hpp"
#include "memory.hpp"

namespace {

// ---------------- Olculer ve renkler ----------------
constexpr int TITLE_H = 26;
constexpr int TASKBAR_H = 36;
constexpr int TERM_COLS = 72;
constexpr int TERM_ROWS = 24;
constexpr int PAD = 4;
constexpr int NOTE_MAX = 2048;

constexpr uint32_t C_BG_TOP = 0x1B3A6B;
constexpr uint32_t C_BG_BOTTOM = 0x5B2A86;
constexpr uint32_t C_BORDER = 0x0E1220;
constexpr uint32_t C_TITLE_ON = 0x2D6CDF;
constexpr uint32_t C_TITLE_OFF = 0x434E6B;
constexpr uint32_t C_BODY = 0xF3F5FA;
constexpr uint32_t C_TEXT = 0x1E2433;
constexpr uint32_t C_TEXT_DIM = 0x5A6478;
constexpr uint32_t C_TERM_BG = 0x0B0F16;
constexpr uint32_t C_TERM_FG = 0xC8F0CE;
constexpr uint32_t C_WHITE = 0xFFFFFF;
constexpr uint32_t C_TASKBAR = 0x0E1220;
constexpr uint32_t C_ACCENT = 0x2D6CDF;

enum Kind { K_TERM = 0, K_SYS, K_ABOUT, K_NOTE, K_COUNT };

struct Window {
    int x, y, w, h;
    const char* title;
    bool open;
    bool minimized;
};

struct Icon {
    int x, y;
    const char* label;
    const char* glyph;
    uint32_t color;
    uint32_t glyph_color;
    Kind kind;
};

struct MenuItem {
    const char* label;
    int action;  // 0-3: pencere ac, 4: yeniden baslat, 5: kapat
};

const MenuItem MENU[] = {{"Terminal", 0}, {"Sistem Bilgisi", 1}, {"Hakkinda", 2},
                         {"Notlar", 3},   {"Yeniden Baslat", 4}, {"Kapat", 5}};
constexpr int MENU_N = 6;
constexpr int MENU_W = 220;
constexpr int MENU_ITEM_H = 30;

Icon g_icons[4];

Window g_win[K_COUNT];
int g_z[K_COUNT];  // acik pencerelerin sirasi, son eleman en ustte
int g_zn = 0;
int g_focus = -1;
bool g_menu = false;

int g_sw = 0, g_sh = 0;  // ekran boyutu
int g_mx = 0, g_my = 0;
uint8_t g_buttons = 0;
bool g_dragging = false;
int g_drag_kind = 0, g_drag_dx = 0, g_drag_dy = 0;

bool g_dirty = true;
uint64_t g_ticks = 0;
uint64_t g_last_half = 0;

char g_term[TERM_ROWS][TERM_COLS];
int g_tr = 0, g_tc = 0;

char g_note[NOTE_MAX];
int g_note_len = 0;

// ---------------- Yardimcilar ----------------
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool in_rect(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

struct Buf {
    char s[96];
    int n;
    void init() {
        n = 0;
        s[0] = 0;
    }
    void str(const char* t) {
        while (*t && n < 95) s[n++] = *t++;
        s[n] = 0;
    }
    void num(uint64_t v) {
        char tmp[21];
        int i = 20;
        tmp[i] = 0;
        if (v == 0) tmp[--i] = '0';
        while (v > 0) {
            tmp[--i] = static_cast<char>('0' + v % 10);
            v /= 10;
        }
        str(&tmp[i]);
    }
    void num2(uint64_t v) {
        if (v < 10) str("0");
        num(v);
    }
};

void fmt_uptime(Buf& b) {
    uint64_t sec = g_ticks / 100;
    b.num2(sec / 3600);
    b.str(":");
    b.num2((sec / 60) % 60);
    b.str(":");
    b.num2(sec % 60);
}

bool blink_on() { return ((g_ticks / 50) & 1) == 0; }

// ---------------- Pencere islemleri ----------------
void z_remove(int k) {
    int j = 0;
    for (int i = 0; i < g_zn; i++)
        if (g_z[i] != k) g_z[j++] = g_z[i];
    g_zn = j;
}

void bring_front(int k) {
    z_remove(k);
    g_z[g_zn++] = k;
}

void focus_top() {
    g_focus = -1;
    for (int i = g_zn - 1; i >= 0; i--) {
        if (!g_win[g_z[i]].minimized) {
            g_focus = g_z[i];
            return;
        }
    }
}

void open_window(int k) {
    g_win[k].open = true;
    g_win[k].minimized = false;
    bring_front(k);
    g_focus = k;
}

void close_window(int k) {
    g_win[k].open = false;
    g_win[k].minimized = false;
    z_remove(k);
    if (g_focus == k) focus_top();
}

void minimize_window(int k) {
    g_win[k].minimized = true;
    if (g_focus == k) focus_top();
}

void taskbar_toggle(int k) {
    Window& w = g_win[k];
    if (!w.open) {
        open_window(k);
    } else if (w.minimized) {
        w.minimized = false;
        bring_front(k);
        g_focus = k;
    } else if (g_focus == k) {
        minimize_window(k);
    } else {
        bring_front(k);
        g_focus = k;
    }
}

int window_at(int x, int y) {
    for (int i = g_zn - 1; i >= 0; i--) {
        const Window& w = g_win[g_z[i]];
        if (!w.minimized && in_rect(x, y, w.x, w.y, w.w, w.h)) return g_z[i];
    }
    return -1;
}

// Gorev cubugundaki dugmelerin yeri (acik pencereler, sabit sirayla)
constexpr int TASK_X0 = 128;
constexpr int TASK_W = 140;
constexpr int TASK_STEP = 146;

int task_slot_of(int k) {
    int slot = 0;
    for (int i = 0; i < K_COUNT; i++) {
        if (!g_win[i].open) continue;
        if (i == k) return slot;
        slot++;
    }
    return -1;
}

int task_button_at(int x, int y) {
    int top = g_sh - TASKBAR_H + 5;
    for (int k = 0; k < K_COUNT; k++) {
        if (!g_win[k].open) continue;
        int slot = task_slot_of(k);
        if (in_rect(x, y, TASK_X0 + slot * TASK_STEP, top, TASK_W, 26)) return k;
    }
    return -1;
}

void menu_rect(int& x, int& y, int& w, int& h) {
    x = 8;
    w = MENU_W;
    h = MENU_N * MENU_ITEM_H + 12;
    y = g_sh - TASKBAR_H - 6 - h;
}

int menu_item_at(int mx, int my) {
    int x, y, w, h;
    menu_rect(x, y, w, h);
    for (int i = 0; i < MENU_N; i++)
        if (in_rect(mx, my, x + 6, y + 6 + i * MENU_ITEM_H, w - 12, MENU_ITEM_H)) return i;
    return -1;
}

void run_menu_action(int action) {
    if (action >= 0 && action <= 3) {
        open_window(action);
    } else if (action == 4) {
        kernel_reboot();
    } else if (action == 5) {
        kernel_shutdown();
    }
}

// ---------------- Fare tiklamasi ----------------
void handle_press() {
    if (g_menu) {
        int idx = menu_item_at(g_mx, g_my);
        g_menu = false;
        if (idx >= 0) run_menu_action(MENU[idx].action);
        return;
    }

    if (g_my >= g_sh - TASKBAR_H) {  // gorev cubugu
        if (in_rect(g_mx, g_my, 8, g_sh - TASKBAR_H + 4, 110, 28)) {
            g_menu = true;
            return;
        }
        int k = task_button_at(g_mx, g_my);
        if (k >= 0) taskbar_toggle(k);
        return;
    }

    int k = window_at(g_mx, g_my);
    if (k >= 0) {
        Window& w = g_win[k];
        bring_front(k);
        g_focus = k;
        if (g_my < w.y + TITLE_H) {
            if (in_rect(g_mx, g_my, w.x + w.w - 26, w.y + 4, 20, 18)) {
                close_window(k);
            } else if (in_rect(g_mx, g_my, w.x + w.w - 50, w.y + 4, 20, 18)) {
                minimize_window(k);
            } else {
                g_dragging = true;
                g_drag_kind = k;
                g_drag_dx = g_mx - w.x;
                g_drag_dy = g_my - w.y;
            }
        }
        return;
    }

    for (int i = 0; i < 4; i++) {
        if (in_rect(g_mx, g_my, g_icons[i].x - 12, g_icons[i].y - 4, 72, 80)) {
            open_window(g_icons[i].kind);
            return;
        }
    }
    g_focus = -1;  // bos masaustune tiklandi
}

// ---------------- Cizim: arka plan, ikonlar ----------------
void draw_background() { gfx_vgradient(0, 0, g_sw, g_sh, C_BG_TOP, C_BG_BOTTOM); }

void draw_icons() {
    for (int i = 0; i < 4; i++) {
        const Icon& ic = g_icons[i];
        bool hover = in_rect(g_mx, g_my, ic.x - 12, ic.y - 4, 72, 80);
        if (hover) gfx_blend_rect(ic.x - 12, ic.y - 4, 72, 80, 0xFFFFFF, 36);
        gfx_blend_rect(ic.x + 2, ic.y + 4, 48, 48, 0x000000, 70);  // golge
        gfx_fill_rect(ic.x, ic.y, 48, 48, ic.color);
        gfx_blend_rect(ic.x, ic.y, 48, 14, 0xFFFFFF, 40);  // ust parlama
        int gw = gfx_text_width(ic.glyph, 2);
        gfx_draw_text(ic.x + (48 - gw) / 2, ic.y + 8, ic.glyph, ic.glyph_color, 2);
        int tw = gfx_text_width(ic.label);
        gfx_draw_text(ic.x + 24 - tw / 2 + 1, ic.y + 57, ic.label, 0x000000);
        gfx_draw_text(ic.x + 24 - tw / 2, ic.y + 56, ic.label, C_WHITE);
    }
}

// ---------------- Cizim: pencere icerikleri ----------------
void info_row(int x, int y, const char* label, const char* value) {
    gfx_draw_text(x, y, label, C_TEXT_DIM);
    gfx_draw_text(x + 104, y, value, C_TEXT);
}

void draw_bar(int x, int y, int w, int h, uint64_t used, uint64_t total, uint32_t color) {
    gfx_fill_rect(x, y, w, h, 0xD5DAE6);
    if (total > 0) {
        uint64_t fill = used * static_cast<uint64_t>(w) / total;
        if (fill > static_cast<uint64_t>(w)) fill = w;
        if (used > 0 && fill == 0) fill = 1;
        gfx_fill_rect(x, y, static_cast<int>(fill), h, color);
    }
    gfx_rect_outline(x, y, w, h, 0xA9B1C4);
}

void draw_terminal(int cx, int cy) {
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = g_term[r][c];
            if (ch > ' ') gfx_draw_char(cx + PAD + c * FONT_W, cy + PAD + r * FONT_H, ch, C_TERM_FG);
        }
    }
    if (g_focus == K_TERM && blink_on())
        gfx_fill_rect(cx + PAD + g_tc * FONT_W, cy + PAD + g_tr * FONT_H + 13, FONT_W, 3, C_TERM_FG);
}

void draw_system(int cx, int cy, int cw) {
    Buf b;
    int x = cx + 14, y = cy + 12;
    gfx_draw_text(x, y, "OSdeneme v0.4", C_ACCENT);
    y += 28;

    b.init();
    b.num(gfx_width());
    b.str(" x ");
    b.num(gfx_height());
    info_row(x, y, "Ekran", b.s);
    y += 22;

    b.init();
    b.num(pmm_total_bytes() >> 20);
    b.str(" MB");
    info_row(x, y, "RAM", b.s);
    y += 22;

    b.init();
    b.num(pmm_free_bytes() >> 20);
    b.str(" MB");
    info_row(x, y, "Bos RAM", b.s);
    y += 22;
    draw_bar(x, y, cw - 28, 10, pmm_total_bytes() - pmm_free_bytes(), pmm_total_bytes(), 0x3DAA6B);
    y += 22;

    b.init();
    b.num(heap_used_bytes() >> 10);
    b.str(" / ");
    b.num(heap_total_bytes() >> 10);
    b.str(" KB");
    info_row(x, y, "Heap", b.s);
    y += 22;
    draw_bar(x, y, cw - 28, 10, heap_used_bytes(), heap_total_bytes(), 0xE08A2D);
    y += 22;

    b.init();
    fmt_uptime(b);
    info_row(x, y, "Calisma", b.s);
    y += 22;

    int open_count = 0;
    for (int i = 0; i < K_COUNT; i++)
        if (g_win[i].open) open_count++;
    b.init();
    b.num(static_cast<uint64_t>(open_count));
    b.str(" acik");
    info_row(x, y, "Pencere", b.s);
}

void draw_about(int cx, int cy) {
    int x = cx + 18, y = cy + 14;
    gfx_draw_text(x, y, "OSdeneme", C_ACCENT, 2);
    y += 40;
    gfx_draw_text(x, y, "Sifirdan yazilmis mini isletim sistemi", C_TEXT);
    y += 32;
    gfx_draw_text(x, y, "Assembly : boot, kesme girisleri", C_TEXT_DIM);
    y += 20;
    gfx_draw_text(x, y, "C++      : cekirdek, bellek, masaustu", C_TEXT_DIM);
    y += 20;
    gfx_draw_text(x, y, "Rust     : seri port, fare, PIC", C_TEXT_DIM);
    y += 32;
    gfx_draw_text(x, y, "Surum 0.4", C_TEXT);
}

void draw_notepad(int cx, int cy, int cw, int ch) {
    int cols = (cw - 2 * PAD - 8) / FONT_W;
    int rows = (ch - 2 * PAD) / FONT_H;
    int x = 0, y = 0;
    for (int i = 0; i < g_note_len; i++) {
        char c = g_note[i];
        if (c == '\n') {
            x = 0;
            y++;
            continue;
        }
        if (x >= cols) {
            x = 0;
            y++;
        }
        if (y < rows) gfx_draw_char(cx + PAD + 4 + x * FONT_W, cy + PAD + y * FONT_H, c, C_TEXT);
        x++;
    }
    if (x >= cols) {
        x = 0;
        y++;
    }
    if (g_focus == K_NOTE && blink_on() && y < rows)
        gfx_fill_rect(cx + PAD + 4 + x * FONT_W, cy + PAD + y * FONT_H + 13, FONT_W, 3, C_TEXT);
    if (g_note_len == 0 && !(g_focus == K_NOTE))
        gfx_draw_text(cx + PAD + 4, cy + PAD, "Yazmak icin buraya tikla...", 0x9AA3B5);
}

void draw_window(int k) {
    const Window& w = g_win[k];
    bool active = (g_focus == k);

    gfx_blend_rect(w.x + 7, w.y + 9, w.w, w.h, 0x000000, 46);
    gfx_blend_rect(w.x + 3, w.y + 4, w.w, w.h, 0x000000, 46);
    gfx_fill_rect(w.x, w.y, w.w, w.h, C_BORDER);
    gfx_fill_rect(w.x + 1, w.y + 1, w.w - 2, TITLE_H - 1, active ? C_TITLE_ON : C_TITLE_OFF);
    gfx_draw_text(w.x + 10, w.y + 5, w.title, C_WHITE);

    // kapat ve kucult dugmeleri
    gfx_fill_rect(w.x + w.w - 26, w.y + 4, 20, 18, 0xD9534F);
    gfx_draw_char(w.x + w.w - 26 + 6, w.y + 5, 'x', C_WHITE);
    gfx_fill_rect(w.x + w.w - 50, w.y + 4, 20, 18, 0x5B6A8A);
    gfx_draw_char(w.x + w.w - 50 + 6, w.y + 5, '-', C_WHITE);

    int cx = w.x + 1, cy = w.y + TITLE_H, cw = w.w - 2, ch = w.h - TITLE_H - 1;
    gfx_fill_rect(cx, cy, cw, ch, k == K_TERM ? C_TERM_BG : C_BODY);
    gfx_set_clip(cx, cy, cw, ch);
    switch (k) {
        case K_TERM: draw_terminal(cx, cy); break;
        case K_SYS: draw_system(cx, cy, cw); break;
        case K_ABOUT: draw_about(cx, cy); break;
        case K_NOTE: draw_notepad(cx, cy, cw, ch); break;
        default: break;
    }
    gfx_reset_clip();
}

// ---------------- Cizim: gorev cubugu, menu, imlec ----------------
void draw_taskbar() {
    int top = g_sh - TASKBAR_H;
    gfx_fill_rect(0, top, g_sw, TASKBAR_H, C_TASKBAR);
    gfx_fill_rect(0, top, g_sw, 1, 0x2A3350);

    gfx_fill_rect(8, top + 4, 110, 28, g_menu ? 0x1D4FA8 : C_ACCENT);
    gfx_draw_text(8 + (110 - gfx_text_width("OSdeneme")) / 2, top + 10, "OSdeneme", C_WHITE);

    for (int k = 0; k < K_COUNT; k++) {
        if (!g_win[k].open) continue;
        int slot = task_slot_of(k);
        int x = TASK_X0 + slot * TASK_STEP;
        bool active = (g_focus == k && !g_win[k].minimized);
        gfx_fill_rect(x, top + 5, TASK_W, 26, active ? 0x2A3A66 : 0x1A2238);
        if (active) gfx_fill_rect(x, top + 29, TASK_W, 2, C_ACCENT);
        char label[16];
        int n = 0;
        const char* t = g_win[k].title;
        while (t[n] && n < 14) {
            label[n] = t[n];
            n++;
        }
        label[n] = 0;
        gfx_draw_text(x + 8, top + 10, label, g_win[k].minimized ? 0x8A94AD : C_WHITE);
    }

    Buf b;
    b.init();
    fmt_uptime(b);
    gfx_draw_text(g_sw - gfx_text_width(b.s) - 14, top + 10, b.s, 0xB8C2DA);
}

void draw_menu() {
    int x, y, w, h;
    menu_rect(x, y, w, h);
    gfx_blend_rect(x + 4, y + 5, w, h, 0x000000, 60);
    gfx_fill_rect(x, y, w, h, 0x2A3350);
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, 0x151B2E);
    int hover = menu_item_at(g_mx, g_my);
    for (int i = 0; i < MENU_N; i++) {
        int iy = y + 6 + i * MENU_ITEM_H;
        if (i == hover) gfx_fill_rect(x + 6, iy, w - 12, MENU_ITEM_H, C_ACCENT);
        gfx_draw_text(x + 20, iy + 7, MENU[i].label, C_WHITE);
        if (i == 3) gfx_fill_rect(x + 12, iy + MENU_ITEM_H - 1, w - 24, 1, 0x2A3350);
    }
}

const char* const CURSOR[19] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ",
    "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ", "X.X X..X    ", "XX  X..X    ",
    "X    X..X   ", "     X..X   ", "      X..X  ", "      X..X  ", "       XX   "};

void draw_cursor() {
    for (int r = 0; r < 19; r++) {
        for (int c = 0; c < 12; c++) {
            char ch = CURSOR[r][c];
            if (ch == 'X') gfx_put_pixel(g_mx + c, g_my + r, 0x000000);
            else if (ch == '.') gfx_put_pixel(g_mx + c, g_my + r, 0xFFFFFF);
        }
    }
}

}  // namespace

// =====================================================================

void desktop_term_clear() {
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++) g_term[r][c] = ' ';
    g_tr = g_tc = 0;
    g_dirty = true;
}

void desktop_term_putc(char c) {
    g_dirty = true;
    if (c == '\r') return;
    if (c == '\b') {
        if (g_tc > 0) g_tc--;
        return;
    }
    if (c == '\n') {
        g_tc = 0;
        g_tr++;
    } else if (c >= ' ' && c < 127) {
        if (g_tc >= TERM_COLS) {
            g_tc = 0;
            g_tr++;
        }
        if (g_tr >= TERM_ROWS) {
            for (int r = 1; r < TERM_ROWS; r++)
                for (int cc = 0; cc < TERM_COLS; cc++) g_term[r - 1][cc] = g_term[r][cc];
            for (int cc = 0; cc < TERM_COLS; cc++) g_term[TERM_ROWS - 1][cc] = ' ';
            g_tr = TERM_ROWS - 1;
        }
        g_term[g_tr][g_tc++] = c;
        return;
    }
    if (g_tr >= TERM_ROWS) {  // satir sonu sonrasi kaydir
        for (int r = 1; r < TERM_ROWS; r++)
            for (int cc = 0; cc < TERM_COLS; cc++) g_term[r - 1][cc] = g_term[r][cc];
        for (int cc = 0; cc < TERM_COLS; cc++) g_term[TERM_ROWS - 1][cc] = ' ';
        g_tr = TERM_ROWS - 1;
    }
}

bool desktop_init() {
    if (!gfx_ready()) return false;
    g_sw = gfx_width();
    g_sh = gfx_height();
    g_mx = g_sw / 2;
    g_my = g_sh / 2;

    int term_w = TERM_COLS * FONT_W + 2 * PAD + 2;
    int term_h = TITLE_H + TERM_ROWS * FONT_H + 2 * PAD + 1;
    g_win[K_TERM] = {104, 24, term_w, term_h, "Terminal", false, false};
    int sys_x = g_sw - 316 - 14;
    if (sys_x < 104 + term_w + 8) sys_x = 104 + term_w + 8;
    g_win[K_SYS] = {sys_x, 24, 316, 300, "Sistem Bilgisi", false, false};
    g_win[K_ABOUT] = {(g_sw - 380) / 2, 180, 380, 240, "Hakkinda", false, false};
    g_win[K_NOTE] = {300, 330, 420, 290, "Notlar", false, false};

    g_icons[0] = {24, 20, "Terminal", ">_", 0x1F2A44, 0x7CFC9A, K_TERM};
    g_icons[1] = {24, 112, "Sistem", "i", 0x2D8CDF, 0xFFFFFF, K_SYS};
    g_icons[2] = {24, 204, "Hakkinda", "?", 0xE08A2D, 0xFFFFFF, K_ABOUT};
    g_icons[3] = {24, 296, "Notlar", "N", 0x3DAA6B, 0xFFFFFF, K_NOTE};

    desktop_term_clear();
    g_zn = 0;
    open_window(K_SYS);
    open_window(K_TERM);  // en son acilan odakta olur
    g_dirty = true;
    return true;
}

void desktop_mouse(int dx, int dy, uint8_t buttons) {
    g_mx = clampi(g_mx + dx, 0, g_sw - 1);
    g_my = clampi(g_my + dy, 0, g_sh - 1);
    bool left = (buttons & 1) != 0;
    bool was_left = (g_buttons & 1) != 0;
    if (left && !was_left) handle_press();
    if (!left) g_dragging = false;
    if (g_dragging && g_win[g_drag_kind].open) {
        Window& w = g_win[g_drag_kind];
        w.x = clampi(g_mx - g_drag_dx, 60 - w.w, g_sw - 60);
        w.y = clampi(g_my - g_drag_dy, 0, g_sh - TASKBAR_H - TITLE_H);
    }
    g_buttons = buttons;
    g_dirty = true;
}

void desktop_key(char c) {
    if (g_focus == K_TERM) {
        shell_input(c);
    } else if (g_focus == K_NOTE) {
        if (c == '\b' || c == 0x7F) {
            if (g_note_len > 0) g_note_len--;
        } else if ((c == '\n' || c == '\r') && g_note_len < NOTE_MAX - 1) {
            g_note[g_note_len++] = '\n';
        } else if (c >= ' ' && c < 127 && g_note_len < NOTE_MAX - 1) {
            g_note[g_note_len++] = c;
        }
    }
    g_dirty = true;
}

void desktop_tick(uint64_t ticks) {
    g_ticks = ticks;
    uint64_t half = ticks / 50;
    if (half != g_last_half) {
        g_last_half = half;
        g_dirty = true;  // saat ve imlec yanip sonmesi
    }
}

bool desktop_needs_render() { return g_dirty; }

void desktop_render() {
    gfx_reset_clip();
    draw_background();
    draw_icons();
    for (int i = 0; i < g_zn; i++) {
        int k = g_z[i];
        if (g_win[k].open && !g_win[k].minimized) draw_window(k);
    }
    draw_taskbar();
    if (g_menu) draw_menu();
    draw_cursor();
    gfx_flip();
    g_dirty = false;
}
