// OSdeneme v0.4 - Masaustu: pencere yoneticisi, gorev cubugu, fare imleci
#pragma once
#include <stdint.h>

bool desktop_init();                                  // gfx hazir olmali
void desktop_mouse(int dx, int dy, uint8_t buttons);  // bit0 sol, bit1 sag, bit2 orta
void desktop_key(char c);                             // odaktaki pencereye
void desktop_tick(uint64_t ticks);                    // 100 Hz sayac
bool desktop_needs_render();
void desktop_render();                                // her seyi arka tampona cizer ve ekrana verir
void desktop_term_putc(char c);                       // kabuk ciktisi Terminal penceresine gelir
void desktop_term_clear();

// kernel.cpp'nin sagladigi fonksiyonlar
extern "C" {
void shell_input(char c);
void kernel_shutdown(void);
void kernel_reboot(void);
}
