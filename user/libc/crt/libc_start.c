#include <stdint.h>
#include "user/libc/include/nlibc.h"

typedef int (*main_t)(int argc, char **argv, char **envp);

extern int main(int argc, char **argv, char **envp);
extern void __libc_env_init(char **envp);

__attribute__((noreturn)) void __libc_start_main(uint64_t *stack) {
    int argc = 0;
    char **argv = (char **)0;
    char **envp = (char **)0;
    int ret;

    if (stack != 0) {
        argc = (int)stack[0];
        argv = (char **)&stack[1];
        envp = argv + argc + 1;
    }

    __libc_env_init(envp);
    ret = ((main_t)main)(argc, argv, envp);
    exit_with_code((uint64_t)ret);
}
