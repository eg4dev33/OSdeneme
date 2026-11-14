# OSdeneme

C++, Assembly (NASM / GNU as) ve Rust ile yazilmis minik bir x86_64 isletim sistemi.
Pencereli, fareli bir masaustu var.

| Katman | Dil | Dosya |
|---|---|---|
| Boot, 32→64 bit gecisi, framebuffer istegi | Assembly | `boot/boot.asm` |
| Kesme giris noktalari | Assembly | `boot/isr.S` |
| Cekirdek, IDT, kabuk, ana dongu | C++ | `kernel/kernel.cpp` |
| Bellek yonetimi (PMM + heap) | C++ | `kernel/memory.cpp` |
| Grafik (cizim, yazi) | C++ | `kernel/gfx.cpp`, `kernel/font.cpp` |
| Framebuffer baglama | C++ | `kernel/fb_hw.cpp` |
| Masaustu (pencere yoneticisi) | C++ | `kernel/desktop.cpp` |
| Seri port, PIC, PIT, klavye tablosu, PS/2 fare | Rust | `kernel_rs/src/lib.rs` |

## Calistirma (GitHub Codespaces)

**Masaustunu tarayicida gormek icin:**

```bash
make desktop
```

Sonra alttaki **PORTS** sekmesinde `6080` portunun yanindaki dunya simgesine tikla,
acilan sayfada adresin sonuna `/vnc.html` ekleyip **Connect**'e bas.

**Sadece metin (seri port) modu:**

```bash
make run
```

Cikmak: `shutdown` yaz ya da `Ctrl+A` sonra `X`.

## Yerelde (kendi bilgisayarinda)

```bash
make run-gui
```

## Yol haritasi
- [x] Boot, 64-bit gecisi, uc dilin birlikte calismasi
- [x] IDT + kesmeler, klavye / seri girdi, mini kabuk
- [x] Fiziksel bellek yoneticisi + heap (kmalloc/kfree)
- [x] Grafik modu (framebuffer), yazi tipi
- [x] Fare, pencere yoneticisi, gorev cubugu, baslat menusu
- [ ] Gorev yonetimi (birden fazla program)
- [ ] Disk surucusu ve dosya sistemi
- [ ] Daha fazla uygulama (hesap makinesi, dosya yoneticisi)
