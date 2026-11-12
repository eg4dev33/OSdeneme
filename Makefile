# Tab yerine ">" kullaniliyor (kopyala-yapistir hatalarini onlemek icin)
.RECIPEPREFIX = >

BUILD := build
CXX   := g++
NASM  := nasm
LD    := ld

CXXFLAGS := -std=c++17 -m64 -O2 -Wall -Wextra \
            -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
            -fno-pie -fno-pic -fno-tree-loop-distribute-patterns \
            -mno-red-zone -mno-sse -mno-sse2 -mno-mmx

RUST_LIB := kernel_rs/target/x86_64-unknown-none/release/libkernel_rs.a

.PHONY: all rust iso run run-gui clean

all: iso

$(BUILD)/boot.o: boot/boot.asm
> mkdir -p $(BUILD)
> $(NASM) -f elf64 $< -o $@

$(BUILD)/isr.o: boot/isr.S
> mkdir -p $(BUILD)
> $(CXX) -m64 -c $< -o $@

$(BUILD)/kernel.o: kernel/kernel.cpp
> mkdir -p $(BUILD)
> $(CXX) $(CXXFLAGS) -c $< -o $@

rust:
> cd kernel_rs && RUSTFLAGS="-C code-model=small -C relocation-model=static" cargo build --release --target x86_64-unknown-none

$(BUILD)/kernel.bin: $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/kernel.o rust linker.ld
> $(LD) -n -T linker.ld -o $@ $(BUILD)/boot.o $(BUILD)/isr.o $(BUILD)/kernel.o $(RUST_LIB)

iso: $(BUILD)/kernel.bin grub/grub.cfg
> mkdir -p $(BUILD)/isofiles/boot/grub
> cp $(BUILD)/kernel.bin $(BUILD)/isofiles/boot/kernel.bin
> cp grub/grub.cfg $(BUILD)/isofiles/boot/grub/grub.cfg
> grub-mkrescue -o $(BUILD)/os.iso $(BUILD)/isofiles

# Codespaces'te ekran yok: terminalden yazdigin tuslar seri port ile OS'a gider.
# Cikmak icin: Ctrl+A sonra X  (ya da OS icinde 'shutdown' yaz)
run: iso
> qemu-system-x86_64 -cdrom $(BUILD)/os.iso -serial mon:stdio -display none -no-reboot

# Bilgisayarinda (yerelde) pencere ile calistirmak icin (PS/2 klavye de calisir)
run-gui: iso
> qemu-system-x86_64 -cdrom $(BUILD)/os.iso -serial stdio -no-reboot

clean:
> rm -rf $(BUILD) kernel_rs/target
