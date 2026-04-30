#include <stdint.h>
#include "user/libc/include/nexos/file.h"

int main(int argc, char **argv) {
    volatile uint64_t numerator = 1u;
    volatile uint64_t denominator = 0u;
    volatile uint64_t result = 0u;

    (void)argc;
    (void)argv;

    write_str("dedemo ELF: about to trigger #DE via divide by zero\n");
    result = numerator / denominator;
    (void)result;
    write_str("dedemo ELF: this line should not appear\n");
    return 0;
}
