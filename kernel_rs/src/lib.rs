//! OSdeneme v0.2 - Rust tarafi: seri port, PIC, PIT (zamanlayici), klavye tablosu.
#![no_std]

use core::arch::asm;
use core::panic::PanicInfo;

const COM1: u16 = 0x3F8;
const PIC1_CMD: u16 = 0x20;
const PIC1_DATA: u16 = 0x21;
const PIC2_CMD: u16 = 0xA0;
const PIC2_DATA: u16 = 0xA1;

unsafe fn outb(port: u16, val: u8) {
    asm!("out dx, al", in("dx") port, in("al") val, options(nomem, nostack, preserves_flags));
}

unsafe fn inb(port: u16) -> u8 {
    let v: u8;
    asm!("in al, dx", out("al") v, in("dx") port, options(nomem, nostack, preserves_flags));
    v
}

// ---------------- Seri port (COM1) ----------------

#[no_mangle]
pub extern "C" fn rust_serial_init() {
    unsafe {
        outb(COM1 + 1, 0x00);
        outb(COM1 + 3, 0x80);
        outb(COM1, 0x03);
        outb(COM1 + 1, 0x00);
        outb(COM1 + 3, 0x03);
        outb(COM1 + 2, 0xC7);
        outb(COM1 + 4, 0x0B);
    }
}

/// Seri porttan veri gelince IRQ4 tetiklensin.
#[no_mangle]
pub extern "C" fn rust_serial_enable_rx_irq() {
    unsafe { outb(COM1 + 1, 0x01) };
}

/// Bekleyen bir bayt varsa dondurur, yoksa -1.
#[no_mangle]
pub extern "C" fn rust_serial_read() -> i32 {
    unsafe {
        if inb(COM1 + 5) & 1 == 0 {
            -1
        } else {
            inb(COM1) as i32
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_serial_write_byte(b: u8) {
    unsafe {
        while inb(COM1 + 5) & 0x20 == 0 {}
        outb(COM1, b);
    }
}

#[no_mangle]
pub extern "C" fn rust_serial_write_str(mut s: *const u8) {
    if s.is_null() {
        return;
    }
    unsafe {
        while *s != 0 {
            if *s == b'\n' {
                rust_serial_write_byte(b'\r');
            }
            rust_serial_write_byte(*s);
            s = s.add(1);
        }
    }
}

// ---------------- PIC (kesme denetleyicisi) ----------------

/// IRQ'lari 32-47 vektorlerine tasir. Acik olanlar: IRQ0 (timer), IRQ1 (klavye), IRQ4 (COM1).
#[no_mangle]
pub extern "C" fn rust_pic_init() {
    unsafe {
        outb(PIC1_CMD, 0x11);
        outb(PIC2_CMD, 0x11);
        outb(PIC1_DATA, 0x20); // master: vektor 32'den baslar
        outb(PIC2_DATA, 0x28); // slave: vektor 40'tan baslar
        outb(PIC1_DATA, 0x04);
        outb(PIC2_DATA, 0x02);
        outb(PIC1_DATA, 0x01);
        outb(PIC2_DATA, 0x01);
        outb(PIC1_DATA, 0xE8); // 1 = kapali; 0,1,2,4 acik
        outb(PIC2_DATA, 0xFF);
    }
}

#[no_mangle]
pub extern "C" fn rust_pic_eoi(irq: u8) {
    unsafe {
        if irq >= 8 {
            outb(PIC2_CMD, 0x20);
        }
        outb(PIC1_CMD, 0x20);
    }
}

// ---------------- PIT (zamanlayici) ----------------

#[no_mangle]
pub extern "C" fn rust_pit_init(hz: u32) {
    let hz = if hz < 19 { 100 } else { hz };
    let div = (1_193_182u32 / hz) as u16;
    unsafe {
        outb(0x43, 0x36);
        outb(0x40, (div & 0xFF) as u8);
        outb(0x40, (div >> 8) as u8);
    }
}

// ---------------- Klavye (PS/2, scancode set 1) ----------------

static MAP: &[u8] = b"\0\x1b1234567890-=\x08\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static MAP_SHIFT: &[u8] = b"\0\x1b!@#$%^&*()_+\x08\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

#[no_mangle]
pub extern "C" fn rust_scancode_to_ascii(sc: u8, shift: u8) -> u8 {
    let table = if shift != 0 { MAP_SHIFT } else { MAP };
    let i = sc as usize;
    if i < table.len() {
        table[i]
    } else {
        0
    }
}

// ---------------- Hesaplama ----------------

#[no_mangle]
pub extern "C" fn rust_fibonacci(n: u32) -> u64 {
    let (mut a, mut b) = (0u64, 1u64);
    for _ in 0..n {
        let t = a.wrapping_add(b);
        a = b;
        b = t;
    }
    a
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { asm!("cli; hlt") };
    }
}
