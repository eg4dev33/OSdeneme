// OSdeneme v0.4 - GRUB'un verdigi framebuffer'i bulur, sayfa tablosuna ekler, gfx'e baglar.
#pragma once
#include <stdint.h>

bool fb_init(uintptr_t multiboot_info);
