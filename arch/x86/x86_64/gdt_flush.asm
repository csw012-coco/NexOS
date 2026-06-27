bits 64

global gdt64_flush
global tss64_flush

section .text

gdt64_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    ret

tss64_flush:
    mov ax, di
    ltr ax
    ret
