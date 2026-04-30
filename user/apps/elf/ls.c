#include "user/apps/elf/nexbox/applets/fs/cmd_ls_shared.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    return cmd_ls_path(argc > 1 ? argv[1] : ".", 0);
}
