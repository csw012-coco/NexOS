#include "test32_libc.h"

int test32_libc_string_memory_case(void) {
    char source[16];
    char target[16];

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
    return 0;
}

int test32_libc_string_ctype_strings_case(void) {
    char target[16];
    char *copy;

    memset(target, 0, sizeof(target));
    strcpy(target, "Ne");
    strcat(target, "xOS");
    copy = strdup("Alpha\n");
    if (copy == 0 ||
        strcmp(target, "NexOS") != 0 ||
        strchr(target, 'x') != target + 2 ||
        strrchr("boot/root", 'o') == 0 ||
        strcmp(strrchr("boot/root", 'o'), "ot") != 0 ||
        strstr("hello i386 libc32", "i386") == 0 ||
        strcasecmp("NeXoS", "nexos") != 0 ||
        strncasecmp("Kernel", "kerb", 3u) != 0 ||
        starts_with("nexos-i386", "nexos") != 1 ||
        ends_with("nexos-i386", "i386") != 1 ||
        streq("same", "same") != 1) {
        return 39;
    }
    strlcpy(target, 4u, "abcdef");
    trim_line(copy);
    if (strcmp(target, "abc") != 0 ||
        strcmp(copy, "Alpha") != 0) {
        return 40;
    }
    free(copy);
    if (!isdigit('7') ||
        !isxdigit('f') ||
        !isalpha('Z') ||
        !isalnum('8') ||
        !isspace('\n') ||
        !isupper('Q') ||
        !islower('q') ||
        tolower('N') != 'n' ||
        toupper('x') != 'X') {
        return 41;
    }
    if (puts("[test32] libc32 string/ctype/strings OK") == EOF) {
        return 42;
    }
    return 0;
}

int test32_libc_header_sync_case(void) {
    bool header_bool = true;

    errno = 0;
    if (!header_bool ||
        CHAR_BIT != 8 ||
        INT_MAX < 2147483647 ||
        UINT32_MAX != 4294967295u ||
        INT32_C(123) != 123 ||
        UINT64_C(0xffffffff) != 0xffffffffULL ||
        EISDIR != 21 ||
        MAP_FAILED != (void *)-1 ||
        IPC_NONBLOCK != SYS_IPC_NONBLOCK) {
        return 46;
    }
    assert(header_bool);
    if (puts("[test32] libc32 header sync OK") == EOF) {
        return 47;
    }
    return 0;
}

int test32_libc_malloc_case(void) {
    char *heap;
    char *grown;
    uint32_t *zeroes;

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
    return 0;
}

int test32_libc_stdlib_numeric_case(void) {
    char *end = 0;

    if (atoi(" \t-42xyz") != -42 ||
        abs(-7) != 7 ||
        labs(-12345L) != 12345L ||
        strtol(" -0x2a!", &end, 0) != -42L ||
        end == 0 ||
        *end != '!' ||
        strtoul("0755 rest", &end, 0) != 493UL ||
        end == 0 ||
        strcmp(end, " rest") != 0 ||
        strtoul("101101", &end, 2) != 45UL ||
        end == 0 ||
        *end != '\0' ||
        strtoull("0xffffffff", &end, 0) != 0xffffffffULL ||
        end == 0 ||
        *end != '\0' ||
        strtoll("-922337203685477580", &end, 10) !=
            -922337203685477580LL ||
        end == 0 ||
        *end != '\0') {
        return 27;
    }
    if (puts("[test32] libc32 stdlib numeric conversion OK") == EOF) {
        return 28;
    }
    return 0;
}

int test32_libc_math_case(void) {
    if (fabs(-3.5) != 3.5 ||
        fabs(sin(0.0)) > 0.000001 ||
        fabs(sin(1.5707963267948966) - 1.0) > 0.000001 ||
        fabs(tan(0.7853981633974483) - 1.0) > 0.000001 ||
        fabs(atan(1.0) - 0.7853981633974483) > 0.000001) {
        return 50;
    }
    if (puts("[test32] libc32 math OK") == EOF) {
        return 51;
    }
    return 0;
}

int test32_libc_stdio_file_format_case(pid_t pid) {
    char io_buffer[64];
    char stdio_path[32];
    FILE *stream;
    int fd;

    if (snprintf(stdio_path,
                 sizeof(stdio_path),
                 "/ram/STDIO%u.TXT",
                 (uint32_t)pid) <= 0) {
        return 43;
    }
    stream = fopen(stdio_path, "w");
    if (stream == 0 ||
        fprintf(stream, "line:%d:%s\n", 32, "stdio") != 14 ||
        fwrite("tail\n", 1u, 5u, stream) != 5u ||
        fflush(stream) != 0 ||
        fclose(stream) != 0) {
        return 29;
    }
    fd = open(stdio_path, O_WRONLY | O_APPEND);
    if (fd < 3 ||
        dprintf(fd, "fd:%x\n", 0x32u) != 6 ||
        close(fd) != 0) {
        return 30;
    }
    stream = fopen(stdio_path, "r");
    if (stream == 0) {
        return 31;
    }
    if (fgets(io_buffer, sizeof(io_buffer), stream) == 0 ||
        strcmp(io_buffer, "line:32:stdio\n") != 0) {
        return 33;
    }
    if (fread(io_buffer, 1u, 5u, stream) != 5u ||
        memcmp(io_buffer, "tail\n", 5u) != 0) {
        return 34;
    }
    if (fgetc(stream) != 'f') {
        return 35;
    }
    if (fgets(io_buffer, sizeof(io_buffer), stream) == 0 ||
        strcmp(io_buffer, "d:32\n") != 0) {
        return 36;
    }
    if (fseek(stream, 5L, SEEK_SET) != 0 ||
        ftell(stream) != 5L ||
        fread(io_buffer, 1u, 2u, stream) != 2u ||
        memcmp(io_buffer, "32", 2u) != 0) {
        return 48;
    }
    if (fseek(stream, -6L, SEEK_END) != 0 ||
        fgets(io_buffer, sizeof(io_buffer), stream) == 0 ||
        strcmp(io_buffer, "fd:32\n") != 0) {
        return 49;
    }
    clearerr(stream);
    if (feof(stream) || ferror(stream)) {
        return 37;
    }
    if (fclose(stream) != 0) {
        return 38;
    }
    if (puts("[test32] libc32 stdio file/format helpers OK") == EOF) {
        return 32;
    }
    return 0;
}
