#include "user/apps/elf/nexbox/applets/fs/cmd_ls_shared.h"

int main(int argc, char **argv, char **envp) {
    const char *path = ".";
    int long_format = 0;
    int show_all = 0;
    int path_set = 0;

    (void)envp;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (uint32_t j = 1; argv[i][j] != '\0'; j++) {
                if (argv[i][j] == 'l') {
                    long_format = 1;
                } else if (argv[i][j] == 'a') {
                    show_all = 1;
                } else {
                    write_err_str("usage: ls [-a] [-l] [path]\n");
                    return 1;
                }
            }
        } else if (!path_set) {
            path = argv[i];
            path_set = 1;
        } else {
            write_err_str("usage: ls [-a] [-l] [path]\n");
            return 1;
        }
    }
    return cmd_ls_path(path, long_format, show_all);
}
