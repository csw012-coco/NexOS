#include <nlibc.h>

int main(int argc, char **argv, char **envp) {
    FILE *file;
    char header[16];
    char *large;
    void *page;

    printf("[app32] argc=%d argv0=%s argv1=%s env0=%s\n",
           argc,
           argc > 0 ? argv[0] : "(none)",
           argc > 1 ? argv[1] : "(none)",
           envp != 0 && envp[0] != 0 ? envp[0] : "(none)");

    page = page_alloc();
    if (page == 0 || page_free(page) != 0) {
        printf("[app32] SYS_PAGE_ALLOC/FREE failed\n");
        return 2;
    }

    large = malloc(96u * 1024u);
    if (large == 0) {
        printf("[app32] dynamic malloc failed\n");
        return 3;
    }
    large[0] = 'N';
    large[96u * 1024u - 1u] = 'X';
    if (large[0] != 'N' || large[96u * 1024u - 1u] != 'X') {
        return 4;
    }
    free(large);
    printf("[app32] dynamic malloc 96KiB OK\n");

    file = fopen("/BOOT/BOOTX.CFG", "r");
    if (file == 0) {
        printf("[app32] fopen failed\n");
        return 5;
    }
    memset(header, 0, sizeof(header));
    if (fread(header, 1u, sizeof(header) - 1u, file) == 0u ||
        fclose(file) != 0) {
        printf("[app32] fread/fclose failed\n");
        return 6;
    }
    if (fprintf(stdout, "[app32] stdio read: %s\n", header) < 0) {
        return 7;
    }

    if (argc > 1 && strcmp(argv[1], "child") == 0) {
        printf("[app32] child exiting 7\n");
        return 7;
    }
    if (argc > 1 && strcmp(argv[1], "exec") == 0) {
        printf("[app32] exec source pid=%d\n", getpid());
        (void)exec("/BOOT/APP32.ELF exec-target");
        return 10;
    }
    if (argc > 1 && strcmp(argv[1], "exec-target") == 0) {
        printf("[app32] exec target pid=%d\n", getpid());
        return 11;
    }

    {
        pid_t child = spawn("/BOOT/APP32.ELF child");
        int status;

        if (child <= 0) {
            printf("[app32] spawn failed\n");
            return 8;
        }
        status = waitpid(child);
        printf("[app32] wait pid=%d status=%d\n", child, status);
        if (status != 7) {
            return 9;
        }
    }

    {
        pid_t child = spawn("/BOOT/APP32.ELF exec");
        int status;

        if (child <= 0) {
            return 10;
        }
        status = waitpid(child);
        printf("[app32] exec/wait pid=%d status=%d\n", child, status);
        if (status != 11) {
            return 11;
        }
    }

    printf("[app32] PASS\n");
    return 0;
}
