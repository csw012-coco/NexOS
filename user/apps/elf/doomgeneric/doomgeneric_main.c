#include "user/libc/include/nlibc.h"
#include "doomgeneric/doomgeneric.h"

int main(int argc, char **argv) {
    printf("doomgeneric: place doom1.wad in the current directory or pass -iwad PATH\n");
    doomgeneric_Create(argc, argv);
    for (;;) {
        doomgeneric_Tick();
    }
}
