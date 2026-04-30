#include "user/libc/include/stdio.h"
#include "user/libc/include/nexos/system.h"

int main(int argc, char **argv) {
    char step_text[2];

    (void)argc;
    (void)argv;

    printf("yielddemo: ELF cooperative yield\n");
    for (uint32_t step = 0; step < 5; step++) {
        step_text[0] = (char)('0' + step);
        step_text[1] = '\0';
        printf("step %s ticks=0x%x\n", step_text, ticks());
        yield();
    }
    printf("yielddemo done\n");
    return 0;
}
