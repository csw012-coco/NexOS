#include <stdint.h>
#include "user/libc/include/nexos/file.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_str("gpfdemo ELF: about to trigger #GP via privileged instruction\n");
    __asm__ __volatile__("cli");
    write_str("gpfdemo ELF: this line should not appear\n");
    return 0;
}
