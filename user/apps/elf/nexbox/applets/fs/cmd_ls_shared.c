#include "user/apps/elf/nexbox/applets/fs/cmd_ls_shared.h"

int cmd_ls_path(const char *path, int long_format) {
    struct syscall_dirent entry;
    int fd;
    int listed = 0;

    fd = opendir(path != NULL && path[0] != '\0' ? path : ".");
    if (fd < 0) {
        write_err_str("opendir failed\n");
        return 1;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (long_format) {
            printf("%c %02x %8u %s\n",
                   (entry.attributes & 0x10u) != 0u ? 'd' : '-',
                   (uint32_t)entry.attributes,
                   entry.size,
                   entry.name);
        } else {
            printf("%s  %s  %u\n", (entry.attributes & 0x10u) != 0 ? "dir" : "file", entry.name, entry.size);
        }
        listed = 1;
    }
    close((uint32_t)fd);
    if (!listed) {
        write_str("<empty>\n");
    }
    return 0;
}
