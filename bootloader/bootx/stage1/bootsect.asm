org 0x7C00
bits 16

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 32
%endif

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc disk_error
    cmp bx, 0xAA55
    jne disk_error

    mov word [dap.sector_count], STAGE2_SECTORS
    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_error

    mov dl, [boot_drive]
    jmp 0x0000:0x8000

disk_error:
    mov si, message
  .next:
    lodsb
    test al, al
    jz .hang
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next
  .hang:
    cli
    hlt
    jmp .hang

boot_drive: db 0
message: db "boot/x stage1 failed", 0

dap:
    db 0x10
    db 0x00
  .sector_count:
    dw 0
    dw 0x8000
    dw 0x0000
    dd 0x00000001
    dd 0x00000000

times 446 - ($ - $$) db 0
times 64 db 0
dw 0xAA55
