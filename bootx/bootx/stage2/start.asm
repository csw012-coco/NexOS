bits 16

global _start
global early_e820_count
global early_e820_entries
extern stage2_main
extern __bss_start
extern __bss_end

section .text
_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    xor ebx, ebx
    xor bp, bp
    mov di, early_e820_entries
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 20
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done

    mov ax, [di + 8]
    or ax, [di + 10]
    or ax, [di + 12]
    or ax, [di + 14]
    jz .e820_next

    add di, 20
    inc bp
    cmp bp, 8
    jae .e820_done

.e820_next:
    test ebx, ebx
    jnz .e820_loop

.e820_done:
    mov [early_e820_count], bp

    in al, 0x92
    or al, 0x02
    out 0x92, al

    lgdt [gdt_descriptor]
    lidt [idt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x18:protected_start

bits 32
protected_start:
    cld
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    movzx eax, byte [boot_drive]
    push eax
    call stage2_main
    add esp, 4

.hang:
    cli
    hlt
    jmp .hang

align 8
gdt:
    dq 0x0000000000000000
    dq 0x00009B000000FFFF
    dq 0x000093000000FFFF
    dq 0x00CF9B000000FFFF
    dq 0x00CF93000000FFFF
gdt_descriptor:
    dw gdt_descriptor - gdt - 1
    dd gdt

idt_descriptor:
    dw 0
    dd 0

boot_drive: db 0

section .data
align 4
early_e820_count: dw 0
align 8
early_e820_entries:
    times (8 * 20) db 0
