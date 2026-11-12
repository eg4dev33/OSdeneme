# OSdeneme v0.2 - Kesme (interrupt) giris noktalari
# 0-31: CPU istisnalari, 32-47: donanim kesmeleri (IRQ0-15)
.intel_syntax noprefix
.extern interrupt_handler

.macro ISR_NOERR n
isr_stub_\n:
    push 0
    push \n
    jmp isr_common
.endm

.macro ISR_ERR n
isr_stub_\n:
    push \n
    jmp isr_common
.endm

.section .text

.irp n, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    ISR_NOERR \n
.endr

.irp n, 8,10,11,12,13,14,17,21,29,30
    ISR_ERR \n
.endr

isr_common:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp            # InterruptFrame* -> C++
    cld
    call interrupt_handler
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16             # vektor no + hata kodu
    iretq

.section .rodata
.global isr_stub_table
.align 8
isr_stub_table:
.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .quad isr_stub_\n
.endr

.section .note.GNU-stack,"",@progbits
