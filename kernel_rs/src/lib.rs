//! OSdeneme v0.4 - Rust tarafi: seri port, PIC, PIT (zamanlayici), klavye tablosu, PS/2 fare.
#![no_std]

use core::arch::asm;
use core::panic::PanicInfo;
use core::sync::atomic::{AtomicU8, Ordering};

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

/// IRQ'lari 32-47 vektorlerine tasir. Acik olanlar: IRQ0 (timer), IRQ1 (klavye), IRQ4 (COM1), IRQ12 (fare).
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
        outb(PIC2_DATA, 0xEF); // slave: yalnizca IRQ12 (fare) acik
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

// ---------------- PS/2 fare ----------------

static MOUSE_IDX: AtomicU8 = AtomicU8::new(0);
static MOUSE_B0: AtomicU8 = AtomicU8::new(0);
static MOUSE_B1: AtomicU8 = AtomicU8::new(0);

/// C++ tarafindaki MouseEvent ile ayni duzende.
#[repr(C)]
pub struct MouseEvent {
    pub dx: i32,
    pub dy: i32,
    pub buttons: u8,
}

fn kbc_wait_write() {
    for _ in 0..100_000 {
        let s = unsafe { inb(0x64) };
        if s & 2 == 0 {
            return;
        }
    }
}

fn kbc_wait_read() {
    for _ in 0..100_000 {
        let s = unsafe { inb(0x64) };
        if s & 1 != 0 {
            return;
        }
    }
}

fn mouse_cmd(b: u8) {
    kbc_wait_write();
    unsafe { outb(0x64, 0xD4) }; // sonraki bayt fareye gitsin
    kbc_wait_write();
    unsafe { outb(0x60, b) };
    kbc_wait_read();
    let _ = unsafe { inb(0x60) }; // ACK (0xFA)
}

#[no_mangle]
pub extern "C" fn rust_mouse_init() {
    // Cikis tamponunda kalanlari at
    for _ in 0..16 {
        let s = unsafe { inb(0x64) };
        if s & 1 != 0 {
            let _ = unsafe { inb(0x60) };
        }
    }
    kbc_wait_write();
    unsafe { outb(0x64, 0xA8) }; // fare portunu ac
    kbc_wait_write();
    unsafe { outb(0x64, 0x20) }; // ayar baytini oku
    kbc_wait_read();
    let mut status = unsafe { inb(0x60) };
    status |= 0x02; // IRQ12 ac
    status &= !0x20; // fare saatini ac
    kbc_wait_write();
    unsafe { outb(0x64, 0x60) };
    kbc_wait_write();
    unsafe { outb(0x60, status) };
    mouse_cmd(0xF6); // varsayilan ayarlar
    mouse_cmd(0xF4); // veri gondermeye basla
}

/// IRQ12'de gelen her bayti verir. 3 baytlik paket tamamlaninca 1 doner ve `out`'u doldurur.
#[no_mangle]
pub extern "C" fn rust_mouse_feed(byte: u8, out: *mut MouseEvent) -> u8 {
    let idx = MOUSE_IDX.load(Ordering::Relaxed);
    if idx == 0 {
        if byte & 0x08 == 0 {
            return 0; // senkron kaybi: ilk bayt degil
        }
        MOUSE_B0.store(byte, Ordering::Relaxed);
        MOUSE_IDX.store(1, Ordering::Relaxed);
        return 0;
    }
    if idx == 1 {
        MOUSE_B1.store(byte, Ordering::Relaxed);
        MOUSE_IDX.store(2, Ordering::Relaxed);
        return 0;
    }
    MOUSE_IDX.store(0, Ordering::Relaxed);
    let flags = MOUSE_B0.load(Ordering::Relaxed);
    if flags & 0xC0 != 0 {
        return 0; // tasma
    }
    let mut dx = MOUSE_B1.load(Ordering::Relaxed) as i32;
    let mut dy = byte as i32;
    if flags & 0x10 != 0 {
        dx -= 256;
    }
    if flags & 0x20 != 0 {
        dy -= 256;
    }
    if !out.is_null() {
        unsafe {
            (*out).dx = dx;
            (*out).dy = -dy; // ekranda y asagi dogru artar
            (*out).buttons = flags & 0x07;
        }
    }
    1
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
