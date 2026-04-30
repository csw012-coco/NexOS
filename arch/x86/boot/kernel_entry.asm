; kernel_entry.asm

bits 32

global _start
extern kernel_main
extern __bss_start
extern __bss_end

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
    cld

    ; .bss 초기화
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

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
