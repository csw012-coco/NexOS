global irq0
irq0:
    cli
    push byte 0
    push byte 32
    jmp irq_common

global irq1
irq1:
    cli
    push byte 0
    push byte 33
    jmp irq_common

extern irq_handler

irq_common:
    pusha
    call irq_handler
    popa
    add esp, 8
    sti
    iret
