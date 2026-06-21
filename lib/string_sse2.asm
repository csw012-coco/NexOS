bits 64

section .text

global memcpy_sse2
global memset_sse2
global memcpy_erms
global memset_erms
global memcpy_wc

memcpy_erms:
    cld
    mov rax, rdi
    mov ecx, edx
    rep movsb
    ret

memset_erms:
    cld
    mov r8, rdi
    mov ecx, edx
    movzx eax, sil
    rep stosb
    mov rax, r8
    ret

; Preserve xmm0 because kernel memory operations can run while a user FPU
; context is active during a syscall or interrupt.
memcpy_sse2:
    cld
    mov rax, rdi
    sub rsp, 16
    movdqu [rsp], xmm0

    mov ecx, edx
    shr ecx, 6
    jz .copy_tail
.copy_loop:
    movdqu xmm0, [rsi]
    movdqu [rdi], xmm0
    movdqu xmm0, [rsi + 16]
    movdqu [rdi + 16], xmm0
    movdqu xmm0, [rsi + 32]
    movdqu [rdi + 32], xmm0
    movdqu xmm0, [rsi + 48]
    movdqu [rdi + 48], xmm0
    add rsi, 64
    add rdi, 64
    dec ecx
    jnz .copy_loop

.copy_tail:
    mov ecx, edx
    and ecx, 63
    rep movsb
    movdqu xmm0, [rsp]
    add rsp, 16
    ret

memset_sse2:
    cld
    mov r9, rdi
    mov r8d, edx
    sub rsp, 16
    movdqu [rsp], xmm0

    movzx eax, sil
    imul eax, eax, 0x01010101
    movd xmm0, eax
    pshufd xmm0, xmm0, 0

    mov ecx, r8d
    shr ecx, 6
    jz .set_tail
.set_loop:
    movdqu [rdi], xmm0
    movdqu [rdi + 16], xmm0
    movdqu [rdi + 32], xmm0
    movdqu [rdi + 48], xmm0
    add rdi, 64
    dec ecx
    jnz .set_loop

.set_tail:
    mov ecx, r8d
    and ecx, 63
    mov eax, esi
    rep stosb
    movdqu xmm0, [rsp]
    add rsp, 16
    mov rax, r9
    ret

; Copy normal cached memory to a write-combining framebuffer mapping.
; Align the destination, use non-temporal stores, then order them with sfence.
memcpy_wc:
    cld
    mov rax, rdi
    sub rsp, 16
    movdqu [rsp], xmm0

    test edx, edx
    jz .wc_done
.wc_align:
    test rdi, 15
    jz .wc_blocks
    movsb
    dec edx
    jnz .wc_align
    jmp .wc_fence

.wc_blocks:
    mov ecx, edx
    shr ecx, 6
    jz .wc_tail
.wc_loop:
    movdqu xmm0, [rsi]
    movntdq [rdi], xmm0
    movdqu xmm0, [rsi + 16]
    movntdq [rdi + 16], xmm0
    movdqu xmm0, [rsi + 32]
    movntdq [rdi + 32], xmm0
    movdqu xmm0, [rsi + 48]
    movntdq [rdi + 48], xmm0
    add rsi, 64
    add rdi, 64
    dec ecx
    jnz .wc_loop

.wc_tail:
    mov ecx, edx
    and ecx, 63
    rep movsb
.wc_fence:
    sfence
.wc_done:
    movdqu xmm0, [rsp]
    add rsp, 16
    ret
