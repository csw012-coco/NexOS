#include <nlibc.h>

int main(void) {
    char source[16];
    char target[16];
    char file_header[4];
    char *heap;
    char *grown;
    uint32_t *zeroes;
    int fd;
    pid_t pid;
    uint32_t ticks_before;
    uint32_t ticks_after;

    if (puts("[test32] ELF32 C program entered Ring 3") == EOF) {
        return 10;
    }

    memset(source, 0, sizeof(source));
    memset(target, 0xa5, sizeof(target));
    memcpy(source, "libc32", 7u);
    memcpy(target, source, 7u);
    memmove(target + 1, target, 7u);
    if (strlen(source) != 6u ||
        strcmp(source, "libc32") != 0 ||
        memcmp(target, "llibc32", 8u) != 0 ||
        strncmp(source, "libc", 4u) != 0) {
        return 11;
    }
    if (puts("[test32] libc32 string/memory OK") == EOF) {
        return 12;
    }

    heap = malloc(32u);
    zeroes = calloc(8u, sizeof(*zeroes));
    if (heap == 0 || zeroes == 0) {
        return 17;
    }
    memcpy(heap, "heap survives realloc", 22u);
    for (uint32_t i = 0; i < 8u; i++) {
        if (zeroes[i] != 0u) {
            return 18;
        }
    }
    grown = realloc(heap, 96u);
    if (grown == 0 || strcmp(grown, "heap survives realloc") != 0) {
        return 19;
    }
    free(zeroes);
    free(grown);
    heap = malloc(32u);
    if (heap == 0) {
        return 20;
    }
    free(heap);
    if (puts("[test32] libc32 malloc/free/calloc/realloc OK") == EOF) {
        return 21;
    }

    pid = getpid();
    if (pid <= 0) {
        return 22;
    }
    if (printf("[test32] printf pid=%u hex=%08x ptr=%p\n",
               (uint32_t)pid,
               0x386u,
               source) < 0) {
        return 23;
    }
    if (snprintf(target,
                 sizeof(target),
                 "%s-%d",
                 "i386",
                 -32) != 8 ||
        strcmp(target, "i386--32") != 0) {
        return 24;
    }
    if (puts("[test32] libc32 getpid/write/puts OK") == EOF) {
        return 13;
    }

    fd = open("/BOOT/TEST32.ELF", O_RDONLY);
    if (fd < 3 ||
        read(fd, file_header, sizeof(file_header)) !=
            (ssize_t)sizeof(file_header) ||
        memcmp(file_header, "\x7f" "ELF", sizeof(file_header)) != 0 ||
        close(fd) != 0 ||
        close(fd) == 0) {
        return 25;
    }
    if (puts("[test32] libc32 open/read/close VFS OK") == EOF) {
        return 26;
    }

    ticks_before = ticks();
    yield();
    ticks_after = ticks();
    if (ticks_after < ticks_before) {
        return 14;
    }
    if (puts("[test32] libc32 ticks/yield OK") == EOF) {
        return 15;
    }

    if (putchar('[') != '[' ||
        puts("test32] libnlibc32.a PASS") == EOF) {
        return 16;
    }
    return 0;
}
