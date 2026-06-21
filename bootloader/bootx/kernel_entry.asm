; kernel_entry.asm

bits 32

global _start
extern kernel_main

; =========================
; Multiboot Header
; =========================
section .multiboot
align 4

dd 0x1BADB002          ; magic
dd 0x00000000          ; flags (최소)
dd -(0x1BADB002)       ; checksum

; =========================
; 코드
; =========================
section .text

_start:
    ; 스택 설정
    mov esp, stack_top

    ; C 커널 호출
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang


; =========================
; 스택
; =========================
section .bss
align 16

stack_bottom:
    resb 16384     ; 16KB

stack_top: