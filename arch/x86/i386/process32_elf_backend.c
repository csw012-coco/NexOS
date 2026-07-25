#include "process32_internal.h"

#include "fs/early_vfs.h"
#include "fs/vfs.h"
#include "paging.h"

int process32_load_exec_plan(const struct process32_exec_plan *plan,
                             struct i386_user_image *image) {
    if (plan == 0 || image == 0 || plan->argc <= 0 || plan->argv[0] == 0) {
        return 0;
    }
    if (g_process32_runtime_vfs != 0 &&
        i386_user_load_elf_space_args_vfs(g_process32_runtime_vfs,
                                          plan->load_path,
                                          0xbfffe000u,
                                          plan->argc,
                                          plan->argv,
                                          plan->envp,
                                          image)) {
        return 1;
    }
    if (g_process32_vfs == 0) {
        return 0;
    }
    return i386_user_load_elf_space_args(g_process32_vfs,
                                         plan->load_path,
                                         0xbfffe000u,
                                         plan->argc,
                                         plan->argv,
                                         plan->envp,
                                         image);
}

void process32_destroy_loaded_image(struct i386_user_image *image) {
    uint32_t current_root;

    if (image == 0 || image->root == 0u) {
        return;
    }
    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    i386_paging_destroy_user_space(image->root);
    if (current_root != image->root) {
        i386_paging_switch(current_root);
    }
    image->root = 0u;
}
