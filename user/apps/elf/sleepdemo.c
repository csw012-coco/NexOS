#include "user/libc/include/stdio.h"
#include "user/libc/include/nexos/system.h"

int main(int argc, char **argv) {
    char step_text[2];

    (void)argc;
    (void)argv;

    printf("sleepdemo: ELF sleep syscall demo\n");
    for (uint32_t step = 0; step < 4; step++) {
        step_text[0] = (char)('0' + step);
        step_text[1] = '\0';
        printf("sleep step %s ticks=0x%x\n", step_text, ticks());
        sleep(10);
    }
    printf("sleepdemo done\n");
    return 0;
}
