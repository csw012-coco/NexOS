#include <nlibc.h>
#include <nexos/audio.h>
#include <nexos/fs.h>
#include <nexos/gfx.h>
#include <nexos/net.h>

__attribute__((noinline)) static uint32_t test32_stack_grow_depth(
    uint32_t depth,
    volatile uint32_t *sink) {
    volatile uint8_t scratch[1024];

    scratch[0] = (uint8_t)depth;
    scratch[sizeof(scratch) - 1u] = (uint8_t)(depth ^ 0xa5u);
    *sink += scratch[0] + scratch[sizeof(scratch) - 1u];
    if (depth == 0u) {
        return *sink;
    }
    return test32_stack_grow_depth(depth - 1u, sink);
}

static int test32_kill_child(void) {
    for (;;) {
        sleep(10u);
    }
    return 0;
}

static int test32_shm_child(void) {
    int shm;
    void *shared;

    shm = shm_open("test32.live", 4096u, 0);
    if (shm <= 0) {
        return 141;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED) {
        return 142;
    }
    if (strcmp((char *)shared, "parent-live") != 0) {
        return 143;
    }
    memcpy(shared, "child-live", 11u);
    if (munmap(shared, 4096u) != 0) {
        return 144;
    }
    return 0;
}

static int test32_stack_grow_case(void) {
    volatile uint32_t stack_sink = 0u;

    if (test32_stack_grow_depth(8u, &stack_sink) == 0u) {
        return 154;
    }
    if (puts("[test32] libc32 page-fault stack grow OK") == EOF) {
        return 155;
    }
    return 0;
}

static int test32_mmap_fixed_partial(void) {
    struct syscall_vm_info vm_info;
    uint32_t base_regions;
    uint32_t base_pages;
    void *mapped;

    if (vm_query(&vm_info) <= 0) {
        return 153;
    }
    base_regions = vm_info.mmap_regions;
    base_pages = vm_info.mmap_pages;
    mapped = mmap((void *)(uintptr_t)0x51000000u,
                  12288u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                  0,
                  0u);
    if (mapped != (void *)(uintptr_t)0x51000000u ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 1u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 153;
    }
    ((char *)mapped)[0] = 'f';
    ((char *)mapped)[8192] = 't';
    if (munmap((void *)(uintptr_t)0x51001000u, 4096u) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 2u ||
        vm_info.mmap_pages != base_pages + 2u ||
        ((char *)mapped)[0] != 'f' ||
        ((char *)mapped)[8192] != 't') {
        return 154;
    }
    if (munmap((void *)(uintptr_t)0x51000000u, 4096u) != 0 ||
        munmap((void *)(uintptr_t)0x51002000u, 4096u) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions ||
        vm_info.mmap_pages != base_pages) {
        return 155;
    }
    if (puts("[test32] libc32 MAP_FIXED/partial munmap OK") == EOF) {
        return 156;
    }
    return 0;
}

static int test32_mmap_prot_child(void) {
    volatile char *mapped;

    mapped = (volatile char *)mmap((void *)(uintptr_t)0x51010000u,
                                   4096u,
                                   PROT_READ,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                   0,
                                   0u);
    if (mapped != (volatile char *)(uintptr_t)0x51010000u) {
        return 157;
    }
    (void)mapped[0];
    mapped[0] = 'x';
    return 158;
}

static int test32_mmap_protection(void) {
    void *mapped;
    pid_t child;

    mapped = mmap((void *)(uintptr_t)0x51010000u,
                  4096u,
                  PROT_READ,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                  0,
                  0u);
    if (mapped != (void *)(uintptr_t)0x51010000u) {
        return 157;
    }
    (void)((volatile char *)mapped)[0];
    if (munmap(mapped, 4096u) != 0) {
        return 158;
    }
    child = spawn_ex("/BOOT/TEST32.ELF mmap-prot-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != -14) {
        return 159;
    }
    if (puts("[test32] libc32 mmap protection fault OK") == EOF) {
        return 160;
    }
    return 0;
}

static int test32_mmap_fault_cleanup_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    pid_t child;

    if (vm_query(&before) <= 0) {
        return 172;
    }
    for (uint32_t i = 0u; i < 4u; i++) {
        child = spawn_ex("/BOOT/TEST32.ELF mmap-prot-child",
                         SYS_SPAWN_ELF,
                         SYS_SPAWN_BACKGROUND);
        if (child <= 0 || waitpid(child) != -14) {
            return 173;
        }
    }
    if (vm_query(&after) <= 0 ||
        after.mmap_regions != before.mmap_regions ||
        after.mmap_pages != before.mmap_pages ||
        after.shared_regions != before.shared_regions ||
        after.shm_mapped_pages != before.shm_mapped_pages) {
        return 174;
    }
    if (puts("[test32] libc32 mmap fault cleanup OK") == EOF) {
        return 175;
    }
    return 0;
}

static int test32_mprotect_child(void) {
    volatile char *mapped;

    mapped = (volatile char *)mmap((void *)(uintptr_t)0x51020000u,
                                   4096u,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                   0,
                                   0u);
    if (mapped != (volatile char *)(uintptr_t)0x51020000u) {
        return 161;
    }
    mapped[0] = 'w';
    if (mprotect((void *)(uintptr_t)0x51020000u, 4096u, PROT_READ) != 0) {
        return 162;
    }
    (void)mapped[0];
    mapped[0] = 'x';
    return 163;
}

static int test32_mprotect_case(void) {
    char *mapped;
    pid_t child;

    mapped = (char *)mmap((void *)(uintptr_t)0x51020000u,
                          4096u,
                          PROT_READ,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          0,
                          0u);
    if (mapped != (char *)(uintptr_t)0x51020000u) {
        return 161;
    }
    if (mprotect(mapped, 4096u, PROT_READ | PROT_WRITE) != 0) {
        return 162;
    }
    mapped[0] = 'm';
    if (mapped[0] != 'm' ||
        mprotect(mapped, 4096u, PROT_READ) != 0 ||
        munmap(mapped, 4096u) != 0) {
        return 163;
    }
    child = spawn_ex("/BOOT/TEST32.ELF mprotect-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != -14) {
        return 164;
    }
    if (puts("[test32] libc32 mprotect OK") == EOF) {
        return 165;
    }
    return 0;
}

static int test32_mprotect_partial_child(void) {
    volatile char *mapped;

    mapped = (volatile char *)mmap((void *)(uintptr_t)0x51030000u,
                                   12288u,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                   0,
                                   0u);
    if (mapped != (volatile char *)(uintptr_t)0x51030000u) {
        return 166;
    }
    mapped[0] = 'a';
    mapped[8192] = 'c';
    if (mprotect((void *)(uintptr_t)0x51031000u,
                 4096u,
                 PROT_READ) != 0) {
        return 167;
    }
    mapped[0] = 'A';
    mapped[8192] = 'C';
    mapped[4096] = 'B';
    return 168;
}

static int test32_mprotect_partial_case(void) {
    struct syscall_vm_info vm_info;
    uint32_t base_regions;
    uint32_t base_pages;
    char *mapped;
    pid_t child;

    if (vm_query(&vm_info) <= 0) {
        return 166;
    }
    base_regions = vm_info.mmap_regions;
    base_pages = vm_info.mmap_pages;
    mapped = (char *)mmap((void *)(uintptr_t)0x51030000u,
                          12288u,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          0,
                          0u);
    if (mapped != (char *)(uintptr_t)0x51030000u ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 1u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 166;
    }
    mapped[0] = 'a';
    mapped[4096] = 'b';
    mapped[8192] = 'c';
    if (mprotect((void *)(uintptr_t)0x51031000u,
                 4096u,
                 PROT_READ) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 3u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 167;
    }
    mapped[0] = 'A';
    mapped[8192] = 'C';
    if (mapped[0] != 'A' ||
        mapped[4096] != 'b' ||
        mapped[8192] != 'C' ||
        mprotect((void *)(uintptr_t)0x51031000u,
                 4096u,
                 PROT_READ | PROT_WRITE) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 3u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 168;
    }
    mapped[4096] = 'B';
    if (mapped[4096] != 'B' ||
        munmap((void *)(uintptr_t)0x51030000u, 4096u) != 0 ||
        munmap((void *)(uintptr_t)0x51031000u, 4096u) != 0 ||
        munmap((void *)(uintptr_t)0x51032000u, 4096u) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions ||
        vm_info.mmap_pages != base_pages) {
        return 169;
    }
    child = spawn_ex("/BOOT/TEST32.ELF mprotect-partial-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != -14) {
        return 170;
    }
    if (puts("[test32] libc32 mprotect partial OK") == EOF) {
        return 171;
    }
    return 0;
}

static int test32_find_mount(const char *target,
                             uint32_t kind,
                             struct syscall_mount_info *out) {
    struct syscall_mount_info candidate;

    if (target == 0 || out == 0) {
        return 0;
    }
    for (uint32_t attempt = 0u; attempt < 4u; attempt++) {
        for (uint32_t i = 0u; i < NOS_MOUNT_SLOT_MAX + 2u; i++) {
            if (mount_query(i, &candidate) > 0 &&
                candidate.kind == kind &&
                strcmp(candidate.target, target) == 0) {
                *out = candidate;
                return 1;
            }
        }
        yield();
    }
    return 0;
}

int main(int argc, char **argv) {
    char source[16];
    char target[16];
    char file_header[4];
    char io_buffer[64];
    struct syscall_dirent dirent;
    char stdio_path[32];
    char *heap;
    char *grown;
    char *copy;
    uint32_t *zeroes;
    FILE *stream;
    int fd;
    pid_t pid;
    char *end;
    char input_ch;
    bool header_bool = true;
    struct syscall_process_info proc_info;
    struct syscall_block_info block_info;
    struct syscall_block_read_info block_read_info;
    struct syscall_block_write_info block_write_info;
    struct syscall_partition_info part_info;
    struct syscall_mount_info mount_info;
    struct syscall_fd_info fd_info;
    struct syscall_tty_info tty_info;
    struct syscall_machine_info machine_info;
    struct syscall_memmap_info memmap_info;
    struct syscall_pmm_info pmm_info;
    struct syscall_vm_info vm_info;
    struct syscall_gfx_info gfx_info_data;
    struct syscall_audio_info audio_info;
    struct syscall_rtl8139_info rtl_info;
    pid_t child;
    char dir_path[32];
    char remove_path[40];
    char mount_path[32];
    char mounted_file[64];
    char nxfs_mount_path[32];
    char mq_message[16];
    void *mapped;
    void *shared;
    int shm;
    mqd_t mq;
    sem_t sem;

    if (argc > 1 && strcmp(argv[1], "kill-child") == 0) {
        return test32_kill_child();
    }
    if (argc > 1 && strcmp(argv[1], "shm-child") == 0) {
        return test32_shm_child();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-fixed") == 0) {
        return test32_mmap_fixed_partial();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-prot-child") == 0) {
        return test32_mmap_prot_child();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-prot") == 0) {
        return test32_mmap_protection();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-cleanup") == 0) {
        return test32_mmap_fault_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect-child") == 0) {
        return test32_mprotect_child();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect") == 0) {
        return test32_mprotect_case();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect-partial-child") == 0) {
        return test32_mprotect_partial_child();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect-partial") == 0) {
        return test32_mprotect_partial_case();
    }
    if (argc > 1 && strcmp(argv[1], "stack-grow") == 0) {
        return test32_stack_grow_case();
    }
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

    end = 0;
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

    pid = getpid();
    if (pid <= 0) {
        return 22;
    }
    memset(&proc_info, 0, sizeof(proc_info));
    for (uint32_t i = 0u; i < SYS_PROC_SLOTS_MAX; i++) {
        struct syscall_process_info candidate;

        if (proc_query(SYS_PROC_QUERY_ALL, i, &candidate) > 0 &&
            candidate.pid == (uint32_t)pid) {
            proc_info = candidate;
            break;
        }
    }
    if (proc_info.pid != (uint32_t)pid) {
        return 53;
    }
    if (proc_info.state != SYS_PROC_STATE_RUNNING) {
        return 55;
    }
    if (proc_info.image_kind != SYS_PROC_IMAGE_ELF) {
        return 56;
    }
    if (puts("[test32] libc32 proc_query syscall OK") == EOF) {
        return 54;
    }
    if (tty_query(STDOUT_FILENO, &tty_info) <= 0 ||
        tty_info.kind != SYS_TTY_KIND_VIRTUAL ||
        tty_info.active != 1u ||
        strcmp(tty_info.path, "/dev/tty0") != 0 ||
        tty_query(9u, &tty_info) != 0) {
        return 66;
    }
    if (fd_query(STDOUT_FILENO, &fd_info) <= 0 ||
        fd_info.fd != STDOUT_FILENO ||
        fd_info.kind != SYS_FD_KIND_TTY_STDOUT ||
        fd_info.writable != 1u ||
        fd_info.readable != 0u ||
        fd_query(15u, &fd_info) != 0) {
        return 70;
    }
    if (machine_info_query(&machine_info) <= 0 ||
        strcmp(machine_info.os_name, "NexOS") != 0 ||
        strcmp(machine_info.arch_name, "i386") != 0 ||
        machine_info.text_columns < 80u ||
        machine_info.text_rows < 25u ||
        machine_info.text_cell_width != 8u ||
        machine_info.text_cell_height == 0u) {
        return 67;
    }
    if (memmap_query(0u, &memmap_info) <= 0 ||
        memmap_info.length == 0u ||
        pmm_query(&pmm_info) <= 0 ||
        pmm_info.total_pages == 0u ||
        pmm_info.free_pages == 0u) {
        return 68;
    }
    if (block_query(0u, &block_info) <= 0 ||
        block_info.index != 0u ||
        block_info.block_size != 512u ||
        block_info.partition_count == 0u ||
        block_query(1u, &block_info) <= 0 ||
        block_info.index != 1u ||
        block_info.block_size != 512u ||
        block_info.partition_count == 0u ||
        part_query(0u, 0u, &part_info) <= 0 ||
        part_info.disk_index != 0u ||
        part_info.part_index != 0u ||
        part_query(1u, 0u, &part_info) <= 0 ||
        part_info.disk_index != 1u ||
        part_info.part_index != 0u) {
        return 81;
    }
    if (mount_query(0u, &mount_info) <= 0 ||
        mount_info.kind != SYS_MOUNT_INFO_FAT32 ||
        strcmp(mount_info.target, "fat") != 0 ||
        mount_info.space_known != 1u ||
        mount_info.block_size == 0u) {
        return 82;
    }
    if (puts("[test32] libc32 query syscalls OK") == EOF) {
        return 69;
    }

    memset(&block_read_info, 0, sizeof(block_read_info));
    if (block_read(0u, 0u, &block_read_info) != 1 ||
        block_read_info.disk_index != 0u ||
        block_read_info.block_size != 512u ||
        block_read_info.bytes_read != 512u) {
        return 107;
    }
    memset(&block_write_info, 0, sizeof(block_write_info));
    block_write_info.bytes_to_write = 1u;
    block_write_info.data[0] = block_read_info.data[0];
    if (block_write(0u, 0u, &block_write_info) != 0 ||
        block_write_info.bytes_written != 0u ||
        block_flush(0u) != 1 ||
        block_flush(0xffffffffu) != 0) {
        return 146;
    }
    memset(&gfx_info_data, 0, sizeof(gfx_info_data));
    if (gfx_info(&gfx_info_data) != 0 ||
        gfx_info_data.width < 80u ||
        gfx_info_data.height < 25u ||
        gfx_info_data.bpp == 0u) {
        return 108;
    }
    memset(&audio_info, 0xa5, sizeof(audio_info));
    if (audio_query(0u, &audio_info) < 0 ||
        (audio_info.present == 0u &&
         (audio_info.initialized != 0u ||
          audio_info.caps != 0u ||
          audio_info.driver_kind != SYS_AUDIO_DRIVER_NONE))) {
        return 109;
    }
    memset(&rtl_info, 0xa5, sizeof(rtl_info));
    if (rtl8139_query(&rtl_info) < 0 ||
        (rtl_info.present == 0u &&
         (rtl_info.initialized != 0u ||
          rtl_info.vendor_id != 0u ||
          rtl_info.device_id != 0u))) {
        return 110;
    }
    if (puts("[test32] libc32 gfx/audio/net/block backend OK") == EOF) {
        return 111;
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
        fd_query((uint32_t)fd, &fd_info) <= 0 ||
        fd_info.kind != SYS_FD_KIND_VFS ||
        fd_info.readable != 1u ||
        fd_info.writable != 0u ||
        strcmp(fd_info.path, "/BOOT/TEST32.ELF") != 0 ||
        read(fd, file_header, sizeof(file_header)) !=
            (ssize_t)sizeof(file_header) ||
        memcmp(file_header, "\x7f" "ELF", sizeof(file_header)) != 0 ||
        fd_query((uint32_t)fd, &fd_info) <= 0 ||
        fd_info.offset != sizeof(file_header) ||
        close(fd) != 0 ||
        close(fd) == 0) {
        return 25;
    }
    if (puts("[test32] libc32 open/read/close VFS OK") == EOF) {
        return 26;
    }

    fd = open("/REDIR32.TXT", O_CREAT | O_TRUNC);
    if (fd < 3 ||
        write(fd, "ok\n", 3u) != 3 ||
        close(fd) != 0) {
        return 112;
    }
    fd = open("/REDIR32.TXT", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, 3u) != 3 ||
        memcmp(io_buffer, "ok\n", 3u) != 0 ||
        close(fd) != 0 ||
        remove("/REDIR32.TXT") != 0) {
        return 113;
    }
    if (puts("[test32] libc32 create/truncate default-write open OK") == EOF) {
        return 114;
    }

    fd = open("/dev/zero", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, 8u) != 8 ||
        io_buffer[0] != 0 ||
        io_buffer[7] != 0 ||
        close(fd) != 0) {
        return 101;
    }
    fd = open("/proc/drivers", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 102;
    }
    fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 124;
    }
    fd = open("/proc/filesystems", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 125;
    }
    fd = open("/proc/block", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 126;
    }
    fd = open("/proc/partitions", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 127;
    }
    fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 131;
    }
    fd = open("/proc/version", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 132;
    }
    fd = open("/proc/fb", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 133;
    }
    fd = open("/proc/interrupts", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 134;
    }
    fd = open("/proc/tty", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 135;
    }
    fd = opendir("/proc");
    if (fd < 3) {
        return 128;
    }
    uint32_t proc_root = 0u;
    while (readdir((uint32_t)fd, &dirent) > 0) {
        if (strcmp(dirent.name, "cpuinfo") == 0) {
            proc_root |= 1u;
        } else if (strcmp(dirent.name, "filesystems") == 0) {
            proc_root |= 2u;
        } else if (strcmp(dirent.name, "block") == 0) {
            proc_root |= 4u;
        } else if (strcmp(dirent.name, "partitions") == 0) {
            proc_root |= 8u;
        } else if (strcmp(dirent.name, "cmdline") == 0) {
            proc_root |= 16u;
        } else if (strcmp(dirent.name, "version") == 0) {
            proc_root |= 32u;
        } else if (strcmp(dirent.name, "fb") == 0) {
            proc_root |= 64u;
        } else if (strcmp(dirent.name, "interrupts") == 0) {
            proc_root |= 128u;
        } else if (strcmp(dirent.name, "tty") == 0) {
            proc_root |= 256u;
        }
    }
    if (close(fd) != 0 || proc_root != 0x1ffu) {
        return 129;
    }
    if (puts("[test32] libc32 procfs enriched files OK") == EOF) {
        return 130;
    }
    fd = open("/event/timer", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 103;
    }
    fd = opendir("/event");
    if (fd < 3) {
        return 118;
    }
    uint32_t event_root = 0u;
    while (readdir((uint32_t)fd, &dirent) > 0) {
        if (strcmp(dirent.name, "timer") == 0) {
            event_root |= 1u;
        } else if (strcmp(dirent.name, "timer.json") == 0) {
            event_root |= 2u;
        } else if ((dirent.attributes & 0x10u) != 0u &&
                   strcmp(dirent.name, "input") == 0) {
            event_root |= 4u;
        } else if ((dirent.attributes & 0x10u) != 0u &&
                   strcmp(dirent.name, "net") == 0) {
            event_root |= 8u;
        } else if ((dirent.attributes & 0x10u) != 0u &&
                   strcmp(dirent.name, "file") == 0) {
            event_root |= 16u;
        } else if ((dirent.attributes & 0x10u) != 0u &&
                   strcmp(dirent.name, "block") == 0) {
            event_root |= 32u;
        } else if ((dirent.attributes & 0x10u) != 0u &&
                   strcmp(dirent.name, "security") == 0) {
            event_root |= 64u;
        }
    }
    if (close(fd) != 0 || event_root != 0x7fu) {
        return 119;
    }
    fd = opendir("/event/net");
    if (fd < 3) {
        return 120;
    }
    uint32_t event_net = 0u;
    while (readdir((uint32_t)fd, &dirent) > 0) {
        if (strcmp(dirent.name, "status") == 0) {
            event_net |= 1u;
        } else if (strcmp(dirent.name, "status.json") == 0) {
            event_net |= 2u;
        }
    }
    if (close(fd) != 0 || event_net != 3u) {
        return 121;
    }
    fd = open("/event/input/keyboard", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 147;
    }
    fd = open("/event/input/keyboard.json", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 148;
    }
    fd = open("/event/input/mouse", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 149;
    }
    fd = open("/event/input/mouse.json", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 150;
    }
    fd = open("/event/timer.json", O_RDONLY);
    if (fd < 3 ||
        read(fd, io_buffer, sizeof(io_buffer)) <= 0 ||
        close(fd) != 0) {
        return 122;
    }
    if (puts("[test32] libc32 eventfs listing/json OK") == EOF) {
        return 123;
    }
    fd = opendir("/cmd");
    if (fd < 3 ||
        readdir((uint32_t)fd, &dirent) <= 0 ||
        close(fd) != 0) {
        return 104;
    }
    fd = opendir("/");
    if (fd < 3) {
        return 115;
    }
    uint32_t root_mounts = 0u;
    while (readdir((uint32_t)fd, &dirent) > 0) {
        if ((dirent.attributes & 0x10u) == 0u) {
            continue;
        }
        if (strcmp(dirent.name, "dev") == 0) {
            root_mounts |= 1u;
        } else if (strcmp(dirent.name, "proc") == 0) {
            root_mounts |= 2u;
        } else if (strcmp(dirent.name, "event") == 0) {
            root_mounts |= 4u;
        } else if (strcmp(dirent.name, "ram") == 0) {
            root_mounts |= 8u;
        }
    }
    if (close(fd) != 0 || root_mounts != 0x0fu) {
        return 116;
    }
    if (puts("[test32] libc32 root mountpoint readdir OK") == EOF) {
        return 117;
    }
    fd = open("/cmd/ls", O_RDONLY);
    if (fd < 3 ||
        read(fd, file_header, 2u) != 2 ||
        file_header[0] != '#' ||
        file_header[1] != '!' ||
        close(fd) != 0) {
        return 105;
    }
    if (puts("[test32] libc32 pseudo fs OK") == EOF) {
        return 106;
    }

    if (write_str("[test32] write_str OK\n") != 22u ||
        write_err_str("[test32] write_err_str OK\n") != 26u ||
        read_char_nonblock(&input_ch) != 0u) {
        return 44;
    }
    if (puts("[test32] libc32 syscall helper wrappers OK") == EOF) {
        return 45;
    }

    child = spawn_ex("/BOOT/TEST32.ELF kill-child", SYS_SPAWN_ELF, 0u);
    if (child <= 0 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 58;
    }
    if (puts("[test32] libc32 kill/wait syscall OK") == EOF) {
        return 59;
    }

    child = spawn_ex("/BOOT/TEST32.ELF kill-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 60;
    }
    if (puts("[test32] libc32 background spawn syscall OK") == EOF) {
        return 61;
    }

    child = spawn_ex("./BOOT/TEST32.ELF 'kill-child'",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 136;
    }
    if (puts("[test32] libc32 relative/quoted spawn OK") == EOF) {
        return 137;
    }

    if (fork() != -1) {
        return 98;
    }
    child = spawn_ex("/BOOT/TEST32.ELF kill-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        fg((uint32_t)child) != 1 ||
        bg((uint32_t)child) != 1 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 99;
    }
    if (puts("[test32] libc32 fork/fg/bg syscall surface OK") == EOF) {
        return 100;
    }

    if (snprintf(stdio_path,
                 sizeof(stdio_path),
                 "/BOOT/STDIO%u.TXT",
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

    if (snprintf(dir_path,
                 sizeof(dir_path),
                 "/BOOT/DIR%u",
                 (uint32_t)pid) <= 0 ||
        snprintf(remove_path,
                 sizeof(remove_path),
                 "%s/RM.TXT",
                 dir_path) <= 0) {
        return 60;
    }
    if (mkdir(dir_path) != 0) {
        return 61;
    }
    stream = fopen(remove_path, "w");
    if (stream == 0 ||
        fwrite("remove\n", 1u, 7u, stream) != 7u ||
        fclose(stream) != 0) {
        return 62;
    }
    if (remove(remove_path) != 0 ||
        fopen(remove_path, "r") != 0) {
        return 63;
    }
    if (rmdir(dir_path) != 0 ||
        opendir(dir_path) >= 0) {
        return 64;
    }
    if (puts("[test32] libc32 mkdir/remove/rmdir syscalls OK") == EOF) {
        return 65;
    }

    if (snprintf(mount_path,
                 sizeof(mount_path),
                 "/MNT%u",
                 (uint32_t)pid) <= 0 ||
        snprintf(mounted_file,
                 sizeof(mounted_file),
                 "%s/BOOT/TEST32.ELF",
                 mount_path) <= 0) {
        return 71;
    }
    if (mkdir(mount_path) != 0 ||
        mount("/dev/disk0p1", mount_path, SYS_MOUNT_AUTO) != 0) {
        return 72;
    }
    if (!test32_find_mount(mount_path + 1,
                           SYS_MOUNT_INFO_FAT32,
                           &mount_info) ||
        mount_info.disk_index != 0u ||
        mount_info.part_index != 0u ||
        mount_info.space_known != 1u ||
        mount_info.block_size == 0u) {
        return 84;
    }
    fd = open(mounted_file, O_RDONLY);
    if (fd < 3 ||
        read(fd, file_header, sizeof(file_header)) !=
            (ssize_t)sizeof(file_header) ||
        memcmp(file_header, "\x7f" "ELF", sizeof(file_header)) != 0 ||
        close(fd) != 0) {
        return 73;
    }
    if (umount(mount_path) != 0 ||
        rmdir(mount_path) != 0) {
        return 74;
    }
    if (puts("[test32] libc32 mount/umount syscalls OK") == EOF) {
        return 75;
    }

    if (snprintf(nxfs_mount_path,
                 sizeof(nxfs_mount_path),
                 "/NX%u",
                 (uint32_t)pid) <= 0) {
        return 76;
    }
    if (mkdir(nxfs_mount_path) != 0 ||
        mount("/dev/disk1p1", nxfs_mount_path, SYS_MOUNT_NXFS) != 0) {
        return 77;
    }
    if (!test32_find_mount(nxfs_mount_path + 1,
                           SYS_MOUNT_INFO_NXFS,
                           &mount_info)) {
        return 83;
    }
    if (mount_info.disk_index != 1u ||
        mount_info.part_index != 0u) {
        return 86;
    }
    if (mount_info.space_known != 1u ||
        mount_info.block_size == 0u) {
        return 88;
    }
    fd = opendir(nxfs_mount_path);
    if (fd < 3 ||
        close(fd) != 0) {
        return 78;
    }
    if (umount(nxfs_mount_path) != 0 ||
        rmdir(nxfs_mount_path) != 0) {
        return 79;
    }
    if (puts("[test32] libc32 nxfs mount syscall OK") == EOF) {
        return 80;
    }

    mapped = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  0,
                  0u);
    if (mapped == MAP_FAILED ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions == 0u ||
        vm_info.mmap_pages == 0u) {
        return 90;
    }
    ((char *)mapped)[0] = 'm';
    ((char *)mapped)[4095] = 'p';
    if (((char *)mapped)[0] != 'm' ||
        ((char *)mapped)[4095] != 'p' ||
        munmap(mapped, 4096u) != 0) {
        return 91;
    }
    if (vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 0u ||
        vm_info.mmap_pages != 0u) {
        return 151;
    }
    mapped = mmap(0,
                  8192u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  0,
                  0u);
    if (mapped == MAP_FAILED ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 1u ||
        vm_info.mmap_pages != 2u) {
        return 98;
    }
    ((char *)mapped)[0] = '2';
    ((char *)mapped)[4096] = 'p';
    ((char *)mapped)[8191] = 'g';
    if (((char *)mapped)[0] != '2' ||
        ((char *)mapped)[4096] != 'p' ||
        ((char *)mapped)[8191] != 'g' ||
        munmap(mapped, 8192u) != 0) {
        return 99;
    }
    if (vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 0u ||
        vm_info.mmap_pages != 0u) {
        return 152;
    }
    shm = shm_open("test32.shm", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.shm_objects == 0u ||
        shm_open("test32.shm", 4096u, SHM_CREATE | SHM_EXCL) >= 0) {
        return 92;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED ||
        vm_query(&vm_info) <= 0 ||
        vm_info.shared_regions != 1u ||
        vm_info.shm_mapped_pages != 1u) {
        return 100;
    }
    memcpy(shared, "shared32", 9u);
    if (munmap(shared, 4096u) != 0) {
        return 101;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED ||
        strcmp((char *)shared, "shared32") != 0 ||
        shm_unlink("test32.shm") != 0 ||
        shm_open("test32.shm", 4096u, 0) >= 0 ||
        munmap(shared, 4096u) != 0) {
        return 102;
    }
    if (vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 0u ||
        vm_info.shm_objects != 0u ||
        vm_info.shm_mapped_pages != 0u ||
        vm_info.user_stack_pages == 0u) {
        return 153;
    }
    if (puts("[test32] libc32 mmap/shm syscalls OK") == EOF) {
        return 93;
    }
    if (test32_mmap_fault_cleanup_case() != 0) {
        return 172;
    }
    shm = shm_open("test32.live", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        return 138;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED) {
        return 139;
    }
    memcpy(shared, "parent-live", 12u);
    child = spawn_ex("/BOOT/TEST32.ELF shm-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != 0 ||
        strcmp((char *)shared, "child-live") != 0 ||
        shm_unlink("test32.live") != 0 ||
        munmap(shared, 4096u) != 0) {
        return 140;
    }
    if (puts("[test32] libc32 live shared mmap OK") == EOF) {
        return 145;
    }

    mq = mq_open("test32.mq", IPC_CREATE | IPC_EXCL);
    if (mq <= 0 ||
        mq_send(mq, "mq32", 5u, IPC_NONBLOCK) != 0 ||
        mq_unlink("test32.mq") != 0 ||
        mq_open("test32.mq", 0) >= 0) {
        return 94;
    }
    memset(mq_message, 0, sizeof(mq_message));
    if (mq_receive(mq, mq_message, sizeof(mq_message), IPC_NONBLOCK) != 5 ||
        strcmp(mq_message, "mq32") != 0) {
        return 95;
    }
    sem = sem_open("test32.sem", 1u, IPC_CREATE | IPC_EXCL);
    if (sem <= 0 ||
        sem_unlink("test32.sem") != 0 ||
        sem_open("test32.sem", 0u, 0) >= 0 ||
        sem_trywait(sem) != 0 ||
        sem_trywait(sem) == 0 ||
        sem_post(sem) != 0 ||
        sem_wait(sem) != 0) {
        return 96;
    }
    if (puts("[test32] libc32 mq/sem syscalls OK") == EOF) {
        return 97;
    }

    (void)ticks();
    yield();
    (void)ticks();
    sleep(1u);
    (void)ticks();
    if (puts("[test32] libc32 ticks/yield/sleep OK") == EOF) {
        return 15;
    }

    if (putchar('[') != '[' ||
        puts("test32] libnlibc32.a PASS") == EOF) {
        return 16;
    }
    return 0;
}
