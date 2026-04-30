#include <stdint.h>
#include "user/libc/include/nexos/file.h"

int main(int argc, char **argv) {
    volatile uint64_t *fault_addr = (volatile uint64_t *)(uintptr_t)0x0000000000001000ull;

    (void)argc;
    (void)argv;

    write_str("pfdemo ELF: about to trigger #PF via unmapped user address\n");
    *fault_addr = 0x1122334455667788ull;
    write_str("pfdemo ELF: this line should not appear\n");
    return 0;
}
