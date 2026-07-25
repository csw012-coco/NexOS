#include <stdio.h>
#include <string.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);

static const char *doomgeneric_iwad_arg(void)
{
    int i;

    for (i = 1; i + 1 < myargc; i++)
    {
        if (strcmp(myargv[i], "-iwad") == 0)
        {
            return myargv[i + 1];
        }
    }
    return NULL;
}

static int doomgeneric_validate_iwad(const char *path)
{
    FILE *file;
    char ident[4];
    size_t read_count;

    if (path == NULL || path[0] == '\0')
    {
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL)
    {
        return 0;
    }
    read_count = fread(ident, 1, sizeof(ident), file);
    fclose(file);
    return read_count == sizeof(ident) &&
           ident[0] == 'I' &&
           ident[1] == 'W' &&
           ident[2] == 'A' &&
           ident[3] == 'D';
}

static void doomgeneric_smoke_runtime(void)
{
    int frame;
    const char *iwad = doomgeneric_iwad_arg();

    if (!doomgeneric_validate_iwad(iwad))
    {
        printf("doomgeneric: smoke IWAD open failed\n");
        exit(22);
    }
    for (frame = 0; frame < doomgeneric_smoke_frames; frame++)
    {
        unsigned int i;

        for (i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; i++)
        {
            unsigned int x = i % DOOMGENERIC_RESX;
            unsigned int y = i / DOOMGENERIC_RESX;
            DG_ScreenBuffer[i] = 0xff000000u |
                                 (((x + (unsigned int)frame * 17u) & 0xffu) << 16) |
                                 (((y + (unsigned int)frame * 11u) & 0xffu) << 8) |
                                 ((x ^ y ^ (unsigned int)frame) & 0xffu);
        }
        DG_DrawFrame();
    }
    printf("doomgeneric: smoke PASS frames=%d\n", doomgeneric_smoke_frames);
    exit(0);
}

void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);

	DG_Init();
    if (doomgeneric_smoke_frames > 0)
    {
        doomgeneric_smoke_runtime();
    }

	D_DoomMain ();
}
