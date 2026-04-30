#include <stdint.h>
#include "user/libc/include/file.h"
#include "user/libc/include/nexos/file.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_str("badptr ELF: about to use invalid user pointer\n");
    (void)write_stdout((const char *)(uintptr_t)0x1ull, 8);
    write_str("badptr ELF: this line should not appear\n");
    return 0;
}
