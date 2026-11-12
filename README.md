# OSdeneme

C++, Assembly (NASM) ve Rust ile yazilmis minik bir x86_64 isletim sistemi.

| Katman | Dil | Dosya |
|---|---|---|
| Boot, 32→64 bit gecisi | Assembly | `boot/boot.asm` |
| Kesme giris noktalari | Assembly | `boot/isr.S` |
| Cekirdek, IDT, kabuk | C++ | `kernel/kernel.cpp` |
| Seri port, PIC, PIT, klavye | Rust | `kernel_rs/src/lib.rs` |

## Calistirma (GitHub Codespaces)

1. Repoda **Code → Codespaces → Create codespace** de.
2. Ortam kurulunca terminalde:

```bash
make run
```

Ekran olmadigi icin cikti terminale seri port uzerinden gelir. Terminale yazdiklarin OS'a gider (`help` dene). Cikmak: `shutdown` yaz ya da `Ctrl+A` sonra `X`.

## Yol haritasi
- [x] IDT + kesmeler
- [x] Klavye / seri girdi + mini kabuk
- [ ] Fiziksel bellek yoneticisi
- [ ] Heap (alloc)
- [ ] Basit kabuk (shell)
