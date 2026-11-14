// OSdeneme v0.4 - Framebuffer'i Multiboot2 bilgisinden okuyup baglar
#include "fb_hw.hpp"

#include "gfx.hpp"
#include "memory.hpp"

extern "C" uint64_t p3_table[512];  // boot.asm

namespace {

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

// boot.asm yalnizca ilk 1 GB'i birebir esliyor. Ekran karti bellegi genelde 3-4 GB arasinda
// oldugu icin, o 1 GB'lik bolgeyi 2 MB'lik sayfalarla birebir esliyoruz.
bool map_framebuffer(uint64_t addr, uint64_t bytes) {
    uint64_t last = (addr + bytes - 1) >> 30;
    for (uint64_t gb = addr >> 30; gb <= last; gb++) {
        if (gb == 0) continue;  // zaten eslenmis
        if (gb >= 512) return false;
        if (p3_table[gb] & 1) continue;
        uint64_t p2 = pmm_alloc_frame();
        if (p2 == 0) return false;
        uint64_t* table = reinterpret_cast<uint64_t*>(p2);
        for (uint64_t i = 0; i < 512; i++) table[i] = ((gb << 30) + (i << 21)) | 0x83;  // present|rw|2MB
        p3_table[gb] = p2 | 0x3;
    }
    uint64_t cr3;
    asm volatile("mov %%cr3, %0\n\tmov %0, %%cr3" : "=&r"(cr3) : : "memory");  // TLB'yi temizle
    return true;
}

}  // namespace

bool fb_init(uintptr_t multiboot_info) {
    const uint8_t* info = reinterpret_cast<const uint8_t*>(multiboot_info);
    uint32_t total = rd32(info);
    const uint8_t* tag = info + 8;
    while (tag < info + total) {
        uint32_t type = rd32(tag);
        uint32_t size = rd32(tag + 4);
        if (type == 0) break;
        if (type == 8 && size >= 38) {  // framebuffer bilgisi
            uint64_t addr = rd64(tag + 8);
            uint32_t pitch = rd32(tag + 16);
            uint32_t width = rd32(tag + 20);
            uint32_t height = rd32(tag + 24);
            uint8_t bpp = tag[28];
            uint8_t fbtype = tag[29];
            if (fbtype != 1 || (bpp != 32 && bpp != 24)) return false;  // dogrudan RGB degil
            if (width < 640 || height < 480 || width > 2048 || height > 1536) return false;
            uint8_t rpos = tag[32], gpos = tag[34], bpos = tag[36];
            if (!map_framebuffer(addr, static_cast<uint64_t>(pitch) * height)) return false;
            return gfx_attach(reinterpret_cast<void*>(addr), pitch, static_cast<int>(width), static_cast<int>(height),
                              bpp, rpos, gpos, bpos);
        }
        tag += (size + 7) & ~static_cast<uint32_t>(7);
    }
    return false;
}
