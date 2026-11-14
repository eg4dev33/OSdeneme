// OSdeneme v0.4 - C++ cekirdek: IDT, kesmeler, giris olaylari, mini kabuk, masaustu
#include <stddef.h>
#include <stdint.h>

#include "desktop.hpp"
#include "fb_hw.hpp"
#include "gfx.hpp"
#include "memory.hpp"

struct MouseEvent {  // kernel_rs/src/lib.rs ile ayni duzen
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
};

// ---- Rust tarafindan saglanan fonksiyonlar (kernel_rs/src/lib.rs) ----
extern "C" {
void rust_serial_init(void);
void rust_serial_enable_rx_irq(void);
int rust_serial_read(void);
void rust_serial_write_str(const char* s);
void rust_pic_init(void);
void rust_pic_eoi(uint8_t irq);
void rust_pit_init(uint32_t hz);
void rust_mouse_init(void);
uint8_t rust_mouse_feed(uint8_t byte, MouseEvent* out);
uint8_t rust_scancode_to_ascii(uint8_t sc, uint8_t shift);
uint64_t rust_fibonacci(uint32_t n);
}

// ---- Assembly tarafindan saglanan kesme giris noktalari (boot/isr.S) ----
extern "C" const uint64_t isr_stub_table[48];

// ---- Derleyicinin ihtiyac duydugu minimum libc ----
extern "C" {
void* memset(void* dst, int c, size_t n) {
    void* ret = dst;
    asm volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(c) : "memory");
    return ret;
}
void* memcpy(void* dst, const void* src, size_t n) {
    void* ret = dst;
    asm volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return ret;
}
void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}
}

namespace {

// ---------------- Port G/C ----------------
inline uint8_t inb(uint16_t port) {
    uint8_t v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
inline void outb(uint16_t port, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
inline void outw(uint16_t port, uint16_t v) { asm volatile("outw %0, %1" : : "a"(v), "Nd"(port)); }

bool g_desktop = false;  // masaustu aciksa kabuk ciktisi Terminal penceresine gider

// ---------------- VGA metin modu (grafik yoksa yedek) ----------------
volatile uint16_t* const VGA = reinterpret_cast<volatile uint16_t*>(0xB8000);
constexpr int WIDTH = 80;
constexpr int HEIGHT = 25;
int g_row = 0;
int g_col = 0;
uint8_t g_color = 0x0A;

void vga_clear() {
    for (int i = 0; i < WIDTH * HEIGHT; i++)
        VGA[i] = (static_cast<uint16_t>(g_color) << 8) | ' ';
    g_row = g_col = 0;
}

void vga_scroll() {
    for (int y = 1; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            VGA[(y - 1) * WIDTH + x] = VGA[y * WIDTH + x];
    for (int x = 0; x < WIDTH; x++)
        VGA[(HEIGHT - 1) * WIDTH + x] = (static_cast<uint16_t>(g_color) << 8) | ' ';
    g_row = HEIGHT - 1;
}

void vga_putc(char c) {
    if (c == '\b') {
        if (g_col > 0) {
            g_col--;
        } else if (g_row > 0) {
            g_row--;
            g_col = WIDTH - 1;
        }
        return;
    }
    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else {
        VGA[g_row * WIDTH + g_col] = (static_cast<uint16_t>(g_color) << 8) | static_cast<uint8_t>(c);
        if (++g_col >= WIDTH) {
            g_col = 0;
            g_row++;
        }
    }
    if (g_row >= HEIGHT) vga_scroll();
}

// Seri porta (Rust) ve ekrana yazar: masaustu aciksa Terminal penceresine, degilse VGA'ya
void print(const char* s) {
    rust_serial_write_str(s);
    if (g_desktop) {
        for (const char* p = s; *p; p++) desktop_term_putc(*p);
    } else {
        for (; *s; s++) vga_putc(*s);
    }
}

void print_u64(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0) {
        buf[--i] = static_cast<char>('0' + v % 10);
        v /= 10;
    }
    print(&buf[i]);
}

void print_hex(uint64_t v) {
    char buf[19];
    int i = 18;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0) {
        buf[--i] = "0123456789abcdef"[v & 0xF];
        v >>= 4;
    }
    buf[--i] = 'x';
    buf[--i] = '0';
    print(&buf[i]);
}

// ---------------- Kucuk string yardimcilari ----------------
bool str_eq(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool str_starts(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

// ---------------- IDT ----------------
struct __attribute__((packed)) IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

struct __attribute__((packed)) IdtPtr {
    uint16_t limit;
    uint64_t base;
};

IdtEntry g_idt[256];

void idt_set(int n, uint64_t handler) {
    g_idt[n].offset_low = static_cast<uint16_t>(handler & 0xFFFF);
    g_idt[n].selector = 0x08;  // boot.asm'deki 64-bit kod segmenti
    g_idt[n].ist = 0;
    g_idt[n].attr = 0x8E;  // present, ring 0, interrupt gate
    g_idt[n].offset_mid = static_cast<uint16_t>((handler >> 16) & 0xFFFF);
    g_idt[n].offset_high = static_cast<uint32_t>(handler >> 32);
    g_idt[n].zero = 0;
}

void idt_init() {
    for (int i = 0; i < 48; i++) idt_set(i, isr_stub_table[i]);
    IdtPtr p;
    p.limit = sizeof(g_idt) - 1;
    p.base = reinterpret_cast<uint64_t>(&g_idt[0]);
    asm volatile("lidt %0" : : "m"(p));
}

// ---------------- Giris olaylari (kesmeler doldurur, ana dongu bosaltir) ----------------
enum EventType : uint8_t { EV_KEY = 0, EV_SERIAL = 1, EV_MOUSE = 2 };

struct InputEvent {
    uint8_t type;
    char ch;
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
};

constexpr uint32_t QSIZE = 256;
InputEvent g_queue[QSIZE];
volatile uint32_t g_qhead = 0;  // kesme yazar
volatile uint32_t g_qtail = 0;  // ana dongu okur

void q_push(const InputEvent& e) {
    uint32_t next = (g_qhead + 1) % QSIZE;
    if (next == g_qtail) return;  // dolu: olayi at
    g_queue[g_qhead] = e;
    asm volatile("" : : : "memory");
    g_qhead = next;
}

bool q_pop(InputEvent& e) {
    if (g_qtail == g_qhead) return false;
    e = g_queue[g_qtail];
    asm volatile("" : : : "memory");
    g_qtail = (g_qtail + 1) % QSIZE;
    return true;
}

// ---------------- Mini kabuk ----------------
constexpr size_t LINE_MAX = 128;
char g_line[LINE_MAX];
size_t g_len = 0;
volatile uint64_t g_ticks = 0;  // 100 Hz

void prompt() { print("osdeneme> "); }

void cmd_help() {
    print("Komutlar:\n");
    print("  help        bu listeyi gosterir\n");
    print("  about       sistem hakkinda\n");
    print("  uptime      acik kalma suresi\n");
    print("  fib <n>     Rust ile fibonacci hesaplar (n <= 90)\n");
    print("  mem         bellek durumunu gosterir\n");
    print("  memtest     heap (kmalloc/kfree) testi yapar\n");
    print("  gui         ekran durumunu gosterir\n");
    print("  echo <yazi> yaziyi tekrar eder\n");
    print("  clear       ekrani temizler\n");
    print("  reboot      yeniden baslatir\n");
    print("  shutdown    kapatir\n");
}

void cmd_mem() {
    print("Kullanilabilir RAM : ");
    print_u64(pmm_total_bytes() >> 20);
    print(" MB\n");
    print("Bos fiziksel bellek: ");
    print_u64(pmm_free_bytes() >> 20);
    print(" MB\n");
    print("Heap               : ");
    print_u64(heap_used_bytes() >> 10);
    print(" KB kullanimda / ");
    print_u64(heap_total_bytes() >> 10);
    print(" KB\n");
}

void cmd_memtest() {
    uint64_t before = heap_used_bytes();
    uint8_t* a = static_cast<uint8_t*>(kmalloc(100));
    uint8_t* b = static_cast<uint8_t*>(kmalloc(5000));
    uint8_t* c = static_cast<uint8_t*>(kmalloc(32));
    print("a=");
    print_hex(reinterpret_cast<uint64_t>(a));
    print(" b=");
    print_hex(reinterpret_cast<uint64_t>(b));
    print(" c=");
    print_hex(reinterpret_cast<uint64_t>(c));
    print("\n");
    if (!a || !b || !c) {
        print("HATA: kmalloc basarisiz\n");
        return;
    }
    for (size_t i = 0; i < 5000; i++) b[i] = static_cast<uint8_t>(i * 7);
    bool ok = true;
    for (size_t i = 0; i < 5000; i++)
        if (b[i] != static_cast<uint8_t>(i * 7)) ok = false;
    print("Yazma/okuma dogrulamasi: ");
    print(ok ? "OK\n" : "HATA\n");
    print("Kullanimda: ");
    print_u64(heap_used_bytes() - before);
    print(" bayt\n");
    kfree(b);
    kfree(a);
    kfree(c);
    print("Hepsi serbest birakildi, fark: ");
    print_u64(heap_used_bytes() - before);
    print(" bayt (0 olmali)\n");
}

void cmd_gui() {
    if (g_desktop) {
        print("Masaustu: acik, ");
        print_u64(static_cast<uint64_t>(gfx_width()));
        print(" x ");
        print_u64(static_cast<uint64_t>(gfx_height()));
        print("\n");
    } else {
        print("Masaustu: kapali (GRUB framebuffer vermedi, metin modundayiz)\n");
    }
}

void run_command(const char* line) {
    while (*line == ' ') line++;
    if (*line == '\0') return;

    if (str_eq(line, "help")) {
        cmd_help();
    } else if (str_eq(line, "about")) {
        print("OSdeneme v0.4: Assembly + C++ + Rust\n");
    } else if (str_eq(line, "uptime")) {
        print("Acik kalma suresi: ");
        print_u64(g_ticks / 100);
        print(" sn (");
        print_u64(g_ticks);
        print(" tick)\n");
    } else if (str_eq(line, "clear")) {
        rust_serial_write_str("\x1b[2J\x1b[H");
        if (g_desktop) {
            desktop_term_clear();
        } else {
            vga_clear();
        }
    } else if (str_starts(line, "echo ")) {
        print(line + 5);
        print("\n");
    } else if (str_starts(line, "fib ")) {
        uint32_t n = 0;
        const char* p = line + 4;
        while (*p >= '0' && *p <= '9' && n < 1000) n = n * 10 + static_cast<uint32_t>(*p++ - '0');
        if (n > 90) {
            print("n en fazla 90 olabilir\n");
        } else {
            print("fib(");
            print_u64(n);
            print(") = ");
            print_u64(rust_fibonacci(n));
            print("\n");
        }
    } else if (str_eq(line, "mem")) {
        cmd_mem();
    } else if (str_eq(line, "memtest")) {
        cmd_memtest();
    } else if (str_eq(line, "gui")) {
        cmd_gui();
    } else if (str_eq(line, "shutdown")) {
        print("Kapatiliyor...\n");
        outw(0x604, 0x2000);  // QEMU ACPI kapatma
        for (;;) asm volatile("cli; hlt");
    } else if (str_eq(line, "reboot")) {
        print("Yeniden baslatiliyor...\n");
        outb(0x64, 0xFE);  // klavye denetleyicisi ile reset
        for (;;) asm volatile("cli; hlt");
    } else {
        print("Bilinmeyen komut: ");
        print(line);
        print("\n'help' yazarak komutlari gorebilirsin.\n");
    }
}

void handle_char(char c) {
    if (c == '\r' || c == '\n') {
        print("\n");
        g_line[g_len] = '\0';
        run_command(g_line);
        g_len = 0;
        prompt();
    } else if (c == 0x08 || c == 0x7F) {
        if (g_len > 0) {
            g_len--;
            print("\b \b");
        }
    } else if (c >= 32 && c < 127 && g_len < LINE_MAX - 1) {
        g_line[g_len++] = c;
        char s[2] = {c, '\0'};
        print(s);
    }
}

// ---------------- Kesme islemcileri (sadece olay kuyruguna yazar) ----------------
bool g_shift = false;
int g_esc_state = 0;  // seri terminaldeki ok tuslari (ESC [ A) gibi dizileri yutmak icin

void keyboard_irq() {
    uint8_t sc = inb(0x60);
    bool release = (sc & 0x80) != 0;
    uint8_t code = sc & 0x7F;
    if (code == 0x2A || code == 0x36) {
        g_shift = !release;
        return;
    }
    if (release) return;
    uint8_t c = rust_scancode_to_ascii(code, g_shift ? 1 : 0);
    if (c) {
        InputEvent e{};
        e.type = EV_KEY;
        e.ch = static_cast<char>(c);
        q_push(e);
    }
}

void serial_irq() {
    for (;;) {
        int ch = rust_serial_read();
        if (ch < 0) break;
        if (g_esc_state == 0) {
            if (ch == 0x1B) {
                g_esc_state = 1;
                continue;
            }
            InputEvent e{};
            e.type = EV_SERIAL;
            e.ch = static_cast<char>(ch);
            q_push(e);
        } else if (g_esc_state == 1) {
            g_esc_state = (ch == '[') ? 2 : 0;
        } else {
            if (ch >= 0x40 && ch <= 0x7E) g_esc_state = 0;
        }
    }
}

void mouse_irq() {
    uint8_t b = inb(0x60);
    MouseEvent ev;
    if (rust_mouse_feed(b, &ev)) {
        InputEvent e{};
        e.type = EV_MOUSE;
        e.dx = static_cast<int16_t>(ev.dx);
        e.dy = static_cast<int16_t>(ev.dy);
        e.buttons = ev.buttons;
        q_push(e);
    }
}

const char* const EXCEPTION_NAMES[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound Range", "Invalid Opcode",
    "Device Not Available", "Double Fault", "Coprocessor Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved", "x87 FP", "Alignment Check",
    "Machine Check", "SIMD FP", "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor Injection", "VMM Communication",
    "Security", "Reserved"};

}  // namespace

// ---------------- Masaustunun cagirdigi sistem fonksiyonlari ----------------
extern "C" void shell_input(char c) { handle_char(c); }

extern "C" void kernel_shutdown(void) {
    print("Kapatiliyor...\n");
    outw(0x604, 0x2000);
    for (;;) asm volatile("cli; hlt");
}

extern "C" void kernel_reboot(void) {
    print("Yeniden baslatiliyor...\n");
    outb(0x64, 0xFE);
    for (;;) asm volatile("cli; hlt");
}

struct InterruptFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

// boot/isr.S tarafindan cagrilir
extern "C" void interrupt_handler(InterruptFrame* f) {
    if (f->int_no < 32) {
        g_color = 0x4F;  // kirmizi zemin (metin modu)
        print("\n*** ISTISNA: ");
        print(EXCEPTION_NAMES[f->int_no]);
        print(" (#");
        print_u64(f->int_no);
        print(") hata=");
        print_hex(f->err_code);
        print(" rip=");
        print_hex(f->rip);
        if (f->int_no == 14) {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            print(" cr2=");
            print_hex(cr2);
        }
        print("\nSistem durduruldu.\n");
        if (g_desktop) desktop_render();  // hatayi ekranda goster
        for (;;) asm volatile("cli; hlt");
    }

    uint8_t irq = static_cast<uint8_t>(f->int_no - 32);
    switch (irq) {
        case 0: g_ticks = g_ticks + 1; break;
        case 1: keyboard_irq(); break;
        case 4: serial_irq(); break;
        case 12: mouse_irq(); break;
        default: break;
    }
    rust_pic_eoi(irq);
}

extern "C" void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    rust_serial_init();
    vga_clear();

    print("OSdeneme v0.4\n");
    print("=============\n");
    print("Assembly: boot + kesme girisleri ... OK\n");
    print("C++:      cekirdek + masaustu ...... OK\n");
    print("Rust:     seri port, PIC, fare ..... OK\n\n");

    if (multiboot_magic != 0x36d76289) print("UYARI: Multiboot2 magic yanlis!\n");

    bool mem_ok = memory_init(multiboot_info);
    if (mem_ok) {
        print("Bellek:   ");
        print_u64(pmm_total_bytes() >> 20);
        print(" MB RAM, heap ");
        print_u64(heap_total_bytes() >> 20);
        print(" MB ........ OK\n");
    } else {
        print("UYARI: bellek haritasi okunamadi!\n");
    }

    bool gui_ok = mem_ok && fb_init(multiboot_info) && desktop_init();

    idt_init();
    rust_pic_init();
    rust_pit_init(100);
    rust_mouse_init();
    rust_serial_enable_rx_irq();

    if (gui_ok) {
        g_desktop = true;  // bundan sonra kabuk ciktisi Terminal penceresine de gider
        print("OSdeneme v0.4 masaustu\n");
        print("Komutlar icin 'help' yaz.\n\n");
    } else {
        print("Grafik modu yok, metin modunda devam ediliyor.\n");
        print("Komutlar icin 'help' yaz.\n\n");
    }
    prompt();

    asm volatile("sti");
    for (;;) {
        InputEvent e;
        while (q_pop(e)) {
            if (e.type == EV_KEY) {
                if (g_desktop) {
                    desktop_key(e.ch);
                } else {
                    handle_char(e.ch);
                }
            } else if (e.type == EV_SERIAL) {
                handle_char(e.ch);  // seri terminalden gelen tuslar her zaman kabuga gider
            } else if (e.type == EV_MOUSE && g_desktop) {
                desktop_mouse(e.dx, e.dy, e.buttons);
            }
        }
        if (g_desktop) {
            desktop_tick(g_ticks);
            if (desktop_needs_render()) desktop_render();
        }
        // Olay yoksa kesme gelene kadar uyu ("sti; hlt" arasinda kesme kacirilmaz)
        asm volatile("cli");
        if (g_qtail == g_qhead) {
            asm volatile("sti; hlt");
        } else {
            asm volatile("sti");
        }
    }
}
