#include <stdint.h>
#include "user/libc/include/nexos/file.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_str("uddemo ELF: about to trigger #UD via ud2\n");
    __asm__ __volatile__("ud2");
    write_str("uddemo ELF: this line should not appear\n");
    return 0;
}
