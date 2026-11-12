; OSdeneme - Assembly boot kodu (Multiboot2 + 32-bit -> 64-bit gecisi)
global start
extern kernel_main

section .multiboot_header
align 8
header_start:
    dd 0xe85250d6                                   ; multiboot2 magic
    dd 0                                            ; mimari: i386
    dd header_end - header_start
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))
    dw 0                                            ; bitis etiketi
    dw 0
    dd 8
header_end:

section .text
bits 32
start:
    mov esp, stack_top
    mov [mb_magic], eax
    mov [mb_info], ebx

    call check_multiboot
    call check_cpuid
    call check_long_mode
    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start

check_multiboot:
    cmp eax, 0x36d76289
    jne .no
    ret
.no:
    mov al, '0'
    jmp error

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no
    ret
.no:
    mov al, '1'
    jmp error

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no
    ret
.no:
    mov al, '2'
    jmp error

setup_page_tables:
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax
    mov eax, p2_table
    or eax, 0b11
    mov [p3_table], eax
    mov ecx, 0
.map_p2:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011                              ; present | writable | huge (2MB)
    mov [p2_table + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2
    ret

enable_paging:
    mov eax, p4_table
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5                                  ; PAE
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8                                  ; long mode enable
    wrmsr
    mov eax, cr0
    or eax, 1 << 31                                 ; paging
    mov cr0, eax
    ret

error:
    mov dword [0xb8000], 0x4f524f45                 ; "ER"
    mov dword [0xb8004], 0x4f3a4f52                 ; "R:"
    mov dword [0xb8008], 0x4f204f20
    mov byte  [0xb800a], al
    hlt

bits 64
long_mode_start:
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov edi, [mb_magic]
    mov esi, [mb_info]
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt

section .rodata
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .bss
align 4096
p4_table: resb 4096
p3_table: resb 4096
p2_table: resb 4096
mb_magic: resd 1
mb_info:  resd 1
stack_bottom: resb 16384
stack_top:
