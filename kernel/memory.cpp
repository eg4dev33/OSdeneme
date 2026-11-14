// OSdeneme v0.3 - Bellek yonetimi
// Not: Ilk 1 GB, boot.asm'de birebir (identity) eslenmis durumda; PMM bu sinirla calisir.
#include "memory.hpp"

extern "C" char __kernel_end[];  // linker.ld

namespace {

constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t MAX_MEMORY = 1ULL << 30;
constexpr size_t MAX_FRAMES = MAX_MEMORY / PAGE_SIZE;
constexpr size_t FIRST_USABLE_FRAME = 256;  // ilk 1 MB (BIOS, VGA, cekirdek) hic dagitilmaz

// ---------------- Fiziksel bellek: bitmap (1 = kullanimda) ----------------
uint64_t g_bitmap[MAX_FRAMES / 64];
uint64_t g_usable_bytes = 0;
uint64_t g_free_frames = 0;

inline bool frame_used(size_t f) { return (g_bitmap[f / 64] >> (f % 64)) & 1; }
inline void set_used(size_t f) { g_bitmap[f / 64] |= 1ULL << (f % 64); }
inline void set_free(size_t f) { g_bitmap[f / 64] &= ~(1ULL << (f % 64)); }

inline uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }
inline uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1); }

inline uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}
inline uint64_t rd64(const uint8_t* p) {
    uint64_t v;
    __builtin_memcpy(&v, p, 8);
    return v;
}

void free_region(uint64_t base, uint64_t len) {
    if (base >= MAX_MEMORY) return;
    uint64_t end = base + len;
    if (end > MAX_MEMORY) end = MAX_MEMORY;
    uint64_t s = align_up(base, PAGE_SIZE);
    uint64_t e = align_down(end, PAGE_SIZE);
    if (e <= s) return;
    for (uint64_t a = s; a < e; a += PAGE_SIZE) set_free(a / PAGE_SIZE);
    g_usable_bytes += e - s;
}

void reserve_range(uint64_t start, uint64_t end) {
    uint64_t s = align_down(start, PAGE_SIZE);
    uint64_t e = align_up(end, PAGE_SIZE);
    if (e > MAX_MEMORY) e = MAX_MEMORY;
    for (uint64_t a = s; a < e; a += PAGE_SIZE) set_used(a / PAGE_SIZE);
}

// Yan yana 'count' bos cerceve bulur ve ayirir; yoksa 0.
uint64_t alloc_contiguous(size_t count) {
    size_t run = 0;
    for (size_t f = FIRST_USABLE_FRAME; f < MAX_FRAMES; f++) {
        if (frame_used(f)) {
            run = 0;
            continue;
        }
        if (++run == count) {
            size_t start = f + 1 - count;
            for (size_t i = start; i <= f; i++) set_used(i);
            g_free_frames -= count;
            return static_cast<uint64_t>(start) * PAGE_SIZE;
        }
    }
    return 0;
}

// ---------------- Heap: birbirine bagli bloklar (first-fit) ----------------
constexpr uint32_t BLOCK_MAGIC = 0xB10C4EAD;

struct alignas(16) Block {
    size_t size;  // yalnizca kullanici verisinin boyutu
    Block* next;  // bellekte hemen sonraki blok
    uint32_t free;
    uint32_t magic;
};

Block* g_heap_head = nullptr;
uint64_t g_heap_bytes = 0;

bool heap_init(size_t frames) {
    uint64_t base = alloc_contiguous(frames);
    if (base == 0) return false;
    g_heap_bytes = static_cast<uint64_t>(frames) * PAGE_SIZE;
    g_heap_head = reinterpret_cast<Block*>(base);
    g_heap_head->size = g_heap_bytes - sizeof(Block);
    g_heap_head->next = nullptr;
    g_heap_head->free = 1;
    g_heap_head->magic = BLOCK_MAGIC;
    return true;
}

}  // namespace

// =====================================================================

bool memory_init(uintptr_t multiboot_info) {
    for (size_t i = 0; i < MAX_FRAMES / 64; i++) g_bitmap[i] = ~0ULL;  // hepsi "kullanimda"
    g_usable_bytes = 0;

    const uint8_t* info = reinterpret_cast<const uint8_t*>(multiboot_info);
    uint32_t total_size = rd32(info);
    bool found_map = false;

    const uint8_t* tag = info + 8;
    while (tag < info + total_size) {
        uint32_t type = rd32(tag);
        uint32_t size = rd32(tag + 4);
        if (type == 0) break;  // bitis etiketi
        if (type == 6 && size >= 16) {  // bellek haritasi
            uint32_t entry_size = rd32(tag + 8);
            if (entry_size >= 24) {
                const uint8_t* e = tag + 16;
                while (e + entry_size <= tag + size) {
                    uint64_t base = rd64(e);
                    uint64_t len = rd64(e + 8);
                    uint32_t etype = rd32(e + 16);
                    if (etype == 1) free_region(base, len);  // 1 = kullanilabilir RAM
                    e += entry_size;
                }
                found_map = true;
            }
        }
        tag += (size + 7) & ~static_cast<uint32_t>(7);
    }
    if (!found_map) return false;

    // Dagitilmamasi gerekenler: ilk 1 MB + cekirdegin kendisi + multiboot bilgisi
    reserve_range(0, FIRST_USABLE_FRAME * PAGE_SIZE);
    reserve_range(0, reinterpret_cast<uint64_t>(__kernel_end));
    reserve_range(multiboot_info, multiboot_info + total_size);

    g_free_frames = 0;
    for (size_t f = 0; f < MAX_FRAMES; f++)
        if (!frame_used(f)) g_free_frames++;

    // 16 MB heap (arka tampon icin de kullaniliyor), olmazsa 4 MB, olmazsa 1 MB
    if (!heap_init(4096) && !heap_init(1024) && !heap_init(256)) return false;
    return true;
}

uint64_t pmm_alloc_frame() {
    for (size_t f = FIRST_USABLE_FRAME; f < MAX_FRAMES; f++) {
        if (!frame_used(f)) {
            set_used(f);
            g_free_frames--;
            return static_cast<uint64_t>(f) * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t phys_addr) {
    size_t f = static_cast<size_t>(phys_addr / PAGE_SIZE);
    if (f < FIRST_USABLE_FRAME || f >= MAX_FRAMES || !frame_used(f)) return;
    set_free(f);
    g_free_frames++;
}

uint64_t pmm_total_bytes() { return g_usable_bytes; }
uint64_t pmm_free_bytes() { return g_free_frames * PAGE_SIZE; }

void* kmalloc(size_t size) {
    if (size == 0 || g_heap_head == nullptr) return nullptr;
    size = (size + 15) & ~static_cast<size_t>(15);

    for (Block* b = g_heap_head; b != nullptr; b = b->next) {
        if (!b->free || b->size < size) continue;

        // Blok yeterince buyukse ikiye bol
        if (b->size >= size + sizeof(Block) + 16) {
            Block* rest = reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(b + 1) + size);
            rest->size = b->size - size - sizeof(Block);
            rest->next = b->next;
            rest->free = 1;
            rest->magic = BLOCK_MAGIC;
            b->size = size;
            b->next = rest;
        }
        b->free = 0;
        return b + 1;
    }
    return nullptr;
}

void kfree(void* ptr) {
    if (ptr == nullptr || g_heap_head == nullptr) return;
    Block* b = static_cast<Block*>(ptr) - 1;
    if (b->magic != BLOCK_MAGIC || b->free) return;  // gecersiz ya da cift free
    b->free = 1;

    // Yan yana bos bloklari birlestir
    for (Block* c = g_heap_head; c != nullptr;) {
        if (c->free && c->next != nullptr && c->next->free) {
            c->size += sizeof(Block) + c->next->size;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
}

uint64_t heap_total_bytes() { return g_heap_bytes; }

uint64_t heap_used_bytes() {
    uint64_t used = 0;
    for (Block* b = g_heap_head; b != nullptr; b = b->next)
        if (!b->free) used += b->size;
    return used;
}
