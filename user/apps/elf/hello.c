#include "user/libc/include/stdio.h"
#include "user/libc/include/nexos/system.h"

int main(int argc, char **argv) {
    printf("hello from ELF user program\n");
    printf("argc: %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d]: %s\n", i, argv[i]);
    }
    printf("ticks: 0x%x\n", ticks());
    return 0;
}
