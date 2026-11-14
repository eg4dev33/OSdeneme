// OSdeneme v0.3 - Bellek yonetimi: fiziksel bellek (PMM) + heap (kmalloc/kfree)
#pragma once
#include <stddef.h>
#include <stdint.h>

// Multiboot2 bilgi yapisindan bellek haritasini okur, PMM ve heap'i kurar.
// Basariliysa true doner.
bool memory_init(uintptr_t multiboot_info);

// ---- Fiziksel bellek (4 KB'lik cerceveler) ----
uint64_t pmm_alloc_frame();              // fiziksel adres, yoksa 0
void pmm_free_frame(uint64_t phys_addr);
uint64_t pmm_total_bytes();              // kullanilabilir toplam RAM
uint64_t pmm_free_bytes();               // henuz kullanilmayan RAM

// ---- Heap ----
void* kmalloc(size_t size);              // 16 bayta hizali, yoksa nullptr
void kfree(void* ptr);
uint64_t heap_total_bytes();
uint64_t heap_used_bytes();
