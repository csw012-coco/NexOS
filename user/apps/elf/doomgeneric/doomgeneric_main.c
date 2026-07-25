#include "user/libc/include/nlibc.h"
#include "doomgeneric/doomgeneric.h"

int doomgeneric_smoke_frames;

static int doom_smoke_frames(int argc, char **argv) {
    int frames = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--smoke-frames") == 0 ||
             strcmp(argv[i], "-smoke-frames") == 0) &&
            i + 1 < argc) {
            frames = atoi(argv[i + 1]);
            if (frames < 1) {
                frames = 1;
            }
        }
    }
    return frames;
}

int main(int argc, char **argv) {
    int smoke_frames = doom_smoke_frames(argc, argv);
    doomgeneric_smoke_frames = smoke_frames;

    printf("doomgeneric: place doom1.wad in the current directory or pass -iwad PATH\n");
    doomgeneric_Create(argc, argv);
    if (smoke_frames > 0) {
        for (int i = 0; i < smoke_frames; i++) {
            doomgeneric_Tick();
        }
        printf("doomgeneric: smoke PASS frames=%d\n", smoke_frames);
        return 0;
    }
    for (;;) {
        doomgeneric_Tick();
    }
}
