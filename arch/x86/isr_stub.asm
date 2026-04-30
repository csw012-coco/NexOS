global isr0
isr0:
    cli
    push byte 0
    push byte 0
    jmp isr_common

global isr1
isr1:
    cli
    push byte 0
    push byte 1
    jmp isr_common

extern isr_handler

isr_common:
    pusha
    call isr_handler
    popa
    add esp, 8
    sti
    iret