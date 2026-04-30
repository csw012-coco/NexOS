#include "kernel/internal/proc/process_elf_internal.h"
#include "kernel/public/mem/vmm.h"
#include "lib/string.h"

static int process_arg_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int process_parse_next_arg(char **cursor_io, char *token_out, uint32_t token_size) {
    char *cursor;
    uint32_t out_len = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (cursor_io == NULL || token_out == NULL || token_size == 0) {
        return 0;
    }
    cursor = *cursor_io;
    while (*cursor != '\0' && process_arg_is_space(*cursor)) {
        cursor++;
    }
    if (*cursor == '\0') {
        token_out[0] = '\0';
        *cursor_io = cursor;
        return 0;
    }

    while (*cursor != '\0') {
        char ch = *cursor;

        if (!single_quote && ch == '\\') {
            cursor++;
            if (*cursor == '\0') {
                break;
            }
            ch = *cursor++;
        } else if (!double_quote && ch == '\'') {
            single_quote = !single_quote;
            cursor++;
            continue;
        } else if (!single_quote && ch == '"') {
            double_quote = !double_quote;
            cursor++;
            continue;
        } else if (!single_quote && !double_quote && process_arg_is_space(ch)) {
            break;
        } else {
            cursor++;
        }

        if (out_len + 1u >= token_size) {
            return 0;
        }
        token_out[out_len++] = ch;
    }

    if (single_quote || double_quote) {
        return 0;
    }

    token_out[out_len] = '\0';
    while (*cursor != '\0' && process_arg_is_space(*cursor)) {
        cursor++;
    }
    *cursor_io = cursor;
    return out_len != 0;
}

static int process_copy_command_line(char *dst, uint32_t dst_size, const char *src) {
    uint32_t len = 0;

    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return 1;
    }
    while (src[len] != '\0') {
        if (len + 1u >= dst_size) {
            dst[0] = '\0';
            return 0;
        }
        dst[len] = src[len];
        len++;
    }
    dst[len] = '\0';
    return 1;
}

static int process_stack_write_u64(uint64_t user_addr, uint64_t value) {
    return addrspace_copy_to_range(user_addr, (const uint8_t *)&value, sizeof(value));
}

int process_extract_command_name(const char *command_line, char *name_out, uint32_t name_out_size) {
    char line[NOS_TTY_LINE_BUFFER_SIZE];
    char *cursor = line;

    if (command_line == NULL || name_out == NULL || name_out_size == 0) {
        return 0;
    }
    if (!process_copy_command_line(line, sizeof(line), command_line)) {
        name_out[0] = '\0';
        return 0;
    }
    return process_parse_next_arg(&cursor, name_out, name_out_size);
}

int process_prepare_arguments(const char *command_line, const char *const *envp, uint64_t *stack_top_out) {
    char line[NOS_TTY_LINE_BUFFER_SIZE];
    char *argv_text[USER_ELF_ARG_MAX];
    uint64_t argv_user[USER_ELF_ARG_MAX];
    const char *env_text[USER_ELF_ENV_MAX];
    uint64_t env_user[USER_ELF_ENV_MAX];
    char *cursor;
    uint32_t argc = 0;
    uint32_t envc = 0;
    uint32_t i;
    uint64_t stack = USER_ELF_STACK_TOP;
    uint64_t block_top;
    uint64_t stack_top;
    uint64_t argc64;
    uint64_t null_value = 0;

    if (stack_top_out == NULL || !process_copy_command_line(line, sizeof(line), command_line)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }

    cursor = line;
    while (*cursor != '\0') {
        char *read_cursor;
        char *next_cursor;
        char *token_start;
        char *write_cursor;
        uint32_t out_len = 0;
        int single_quote = 0;
        int double_quote = 0;

        while (*cursor != '\0' && process_arg_is_space(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (argc >= USER_ELF_ARG_MAX) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
            return 0;
        }
        read_cursor = cursor;
        token_start = cursor;
        write_cursor = cursor;
        argv_text[argc++] = token_start;
        while (*read_cursor != '\0') {
            char ch = *read_cursor;

            if (!single_quote && ch == '\\') {
                read_cursor++;
                if (*read_cursor == '\0') {
                    break;
                }
                ch = *read_cursor++;
            } else if (!double_quote && ch == '\'') {
                single_quote = !single_quote;
                read_cursor++;
                continue;
            } else if (!single_quote && ch == '"') {
                double_quote = !double_quote;
                read_cursor++;
                continue;
            } else if (!single_quote && !double_quote && process_arg_is_space(ch)) {
                break;
            } else {
                read_cursor++;
            }

            *write_cursor++ = ch;
            out_len++;
        }
        if (single_quote || double_quote) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
            return 0;
        }
        next_cursor = read_cursor;
        while (*next_cursor != '\0' && process_arg_is_space(*next_cursor)) {
            next_cursor++;
        }
        *write_cursor++ = '\0';
        cursor = next_cursor;
    }

    if (argc == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_BAD_ARGS;
        return 0;
    }

    while (envp != NULL && envp[envc] != NULL && envc < USER_ELF_ENV_MAX) {
        env_text[envc] = envp[envc];
        envc++;
    }

    for (i = envc; i > 0; i--) {
        uint32_t len = str_len(env_text[i - 1u]) + 1u;

        if (stack < USER_ELF_STACK_BOTTOM + len) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
            return 0;
        }
        stack -= len;
        if (!addrspace_copy_to_range(stack, (const uint8_t *)env_text[i - 1u], len)) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
            return 0;
        }
        env_user[i - 1u] = stack;
    }

    for (i = argc; i > 0; i--) {
        uint32_t len = str_len(argv_text[i - 1u]) + 1u;

        if (stack < USER_ELF_STACK_BOTTOM + len) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
            return 0;
        }
        stack -= len;
        if (!addrspace_copy_to_range(stack, (const uint8_t *)argv_text[i - 1u], len)) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
            return 0;
        }
        argv_user[i - 1u] = stack;
    }

    block_top = stack & ~0x7ull;
    if (block_top < USER_ELF_STACK_BOTTOM) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        return 0;
    }

    stack_top =
        (block_top - ((uint64_t)(argc + envc + 3u) * sizeof(uint64_t))) & ~0xFull;
    if (stack_top < USER_ELF_STACK_BOTTOM) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        return 0;
    }

    argc64 = argc;
    if (!process_stack_write_u64(stack_top, argc64)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        return 0;
    }
    for (i = 0; i < argc; i++) {
        if (!process_stack_write_u64(stack_top + (uint64_t)(i + 1u) * sizeof(uint64_t), argv_user[i])) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
            return 0;
        }
    }
    if (!process_stack_write_u64(stack_top + (uint64_t)(argc + 1u) * sizeof(uint64_t), null_value)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        return 0;
    }
    for (i = 0; i < envc; i++) {
        if (!process_stack_write_u64(stack_top + (uint64_t)(argc + 2u + i) * sizeof(uint64_t), env_user[i])) {
            g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
            return 0;
        }
    }
    if (!process_stack_write_u64(stack_top + (uint64_t)(argc + 2u + envc) * sizeof(uint64_t), null_value)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_STACK_ALLOC;
        return 0;
    }

    *stack_top_out = stack_top;
    return 1;
}

static void process_init_bound_elf_process(const struct process *proc) {
    g_bound_session->process = *proc;
    g_bound_session->process.image_kind = PROCESS_IMAGE_ELF;
    g_bound_session->process.address_space = &g_bound_session->address_space;
    g_bound_session->process.state = PROCESS_STATE_RUNNING;
    g_bound_session->process.exit_code = 0;
}

static int process_create_elf_address_space(void) {
    g_bound_session->address_space.kernel_cr3 = vmm_current_root();
    g_bound_session->address_space.user_cr3 = vmm_create_user_root();
    if (g_bound_session->address_space.user_cr3 == 0) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    return 1;
}

static int process_bound_user_root_active(void) {
    return g_bound_session->address_space.user_cr3 != 0 &&
           vmm_root_is_current(g_bound_session->address_space.user_cr3);
}

static int process_prepare_elf_address_space(void) {
    if (!vmm_switch_root_or_fail(g_bound_session->address_space.user_cr3)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    addrspace_unmap_range_if_present(USER_ELF_BASE, USER_ELF_LIMIT);
    addrspace_unmap_range_if_present(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    vmm_allow_user_range(USER_ELF_BASE, USER_ELF_LIMIT);
    vmm_allow_user_range(USER_ELF_STACK_BOTTOM, USER_ELF_STACK_TOP);
    return process_bound_user_root_active();
}

static int process_load_elf_segment(const uint8_t *image,
                                    uint32_t image_size,
                                    const struct elf64_phdr *phdr) {
    if (!process_bound_user_root_active()) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    if (phdr->p_offset + phdr->p_filesz > image_size || phdr->p_filesz > phdr->p_memsz) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_BOUNDS;
        return 0;
    }
    if (phdr->p_vaddr < USER_ELF_BASE || phdr->p_vaddr + phdr->p_memsz > USER_ELF_LIMIT) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_ADDR;
        return 0;
    }
    if (!addrspace_map_range_with_perms(phdr->p_vaddr,
                                        phdr->p_vaddr + phdr->p_memsz,
                                        ((phdr->p_flags & ELF_PF_W) != 0 ? VMM_PERM_WRITE : VMM_PERM_NONE) |
                                            ((phdr->p_flags & ELF_PF_X) != 0 ? VMM_PERM_EXEC : VMM_PERM_NONE)) ||
        !addrspace_zero_range(phdr->p_vaddr, phdr->p_memsz) ||
        !addrspace_copy_to_range(phdr->p_vaddr, image + phdr->p_offset, phdr->p_filesz)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    return 1;
}

int process_begin_elf_session(void) {
    const struct process *parent_proc =
        g_user_session.process.image_kind != PROCESS_IMAGE_NONE ? &g_user_session.process : NULL;
    struct process *proc = process_alloc_slot(&g_user_session, parent_proc);

    if (proc == NULL) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ENTER;
        return 0;
    }
    addrspace_release_dynamic_pages();

    if (!process_create_elf_address_space()) {
        g_process_slot_used[proc->slot] = 0;
        process_clear_slot_state(proc);
        return 0;
    }

    if (!process_prepare_elf_address_space()) {
        session_finish(&g_user_session, g_user_page_mappings);
        return 0;
    }
    process_init_bound_elf_process(proc);
    return 1;
}

static int process_validate_elf_image(const struct elf64_ehdr *ehdr, uint32_t image_size) {
    if (image_size < sizeof(*ehdr)) {
        return 0;
    }
    if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        return 0;
    }
    if (ehdr->e_ident[4] != ELF_CLASS_64 || ehdr->e_ident[5] != ELF_DATA_LSB) {
        return 0;
    }
    if (ehdr->e_type != ELF_ET_EXEC || ehdr->e_machine != ELF_EM_X86_64) {
        return 0;
    }
    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize > image_size) {
        return 0;
    }
    if (ehdr->e_phentsize != sizeof(struct elf64_phdr)) {
        return 0;
    }
    return 1;
}

int process_load_elf_image(const uint8_t *image, uint32_t image_size, uint64_t *entry_out) {
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr *)image;
    const struct elf64_phdr *phdrs = (const struct elf64_phdr *)(image + ehdr->e_phoff);

    if (!process_validate_elf_image(ehdr, image_size)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_HEADER;
        return 0;
    }

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf64_phdr *phdr = &phdrs[i];

        if (phdr->p_type != ELF_PT_LOAD || phdr->p_memsz == 0) {
            continue;
        }
        if (!process_load_elf_segment(image, image_size, phdr)) {
            return 0;
        }
    }

    *entry_out = ehdr->e_entry;
    return 1;
}
