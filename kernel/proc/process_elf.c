#include "kernel/internal/proc/process_elf_internal.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/mem/vmm.h"
#include "hal/hal.h"
#include "lib/string.h"

enum {
    PROCESS_ELF_BACKING_MAX = USER_PROCESS_LIMIT + 1u,
    PROCESS_ELF_BACKING_PAGE_MAX = NOS_ELF_FILE_BUFFER_SIZE / USER_PAGE_SIZE,
    PROCESS_ELF_BACKING_NONE = 0xffu
};

struct process_elf_backing {
    uint8_t used;
    uint16_t refs;
    uint32_t size;
    uint64_t pages[PROCESS_ELF_BACKING_PAGE_MAX];
};

static struct process_elf_backing g_process_elf_backings[PROCESS_ELF_BACKING_MAX];

static struct process_elf_backing *process_elf_backing_get(uint8_t slot) {
    if (slot >= PROCESS_ELF_BACKING_MAX || !g_process_elf_backings[slot].used) {
        return 0;
    }
    return &g_process_elf_backings[slot];
}

void process_release_elf_backing(struct process_session *session) {
    struct process_elf_backing *backing;

    if (session == 0) {
        return;
    }
    backing = process_elf_backing_get(session->elf_backing_slot);
    if (backing != 0 && backing->refs != 0 && --backing->refs == 0) {
        uint32_t page_count = (backing->size + USER_PAGE_SIZE - 1u) / USER_PAGE_SIZE;

        for (uint32_t i = 0; i < page_count; i++) {
            (void)pmm_release_page(backing->pages[i]);
            backing->pages[i] = 0;
        }
        backing->used = 0;
        backing->size = 0;
    }
    session->elf_backing_slot = PROCESS_ELF_BACKING_NONE;
    session->elf_image_size = 0;
    session->elf_segment_count = 0;
}

int process_retain_elf_backing(struct process_session *session) {
    struct process_elf_backing *backing;

    if (session == 0) {
        return 0;
    }
    backing = process_elf_backing_get(session->elf_backing_slot);
    if (backing == 0 || backing->refs == 0xffffu) {
        return 0;
    }
    backing->refs++;
    return 1;
}

static int process_create_elf_backing(struct process_session *session,
                                      const uint8_t *image,
                                      uint32_t image_size) {
    struct process_elf_backing *backing = 0;
    uint32_t page_count = (image_size + USER_PAGE_SIZE - 1u) / USER_PAGE_SIZE;

    process_release_elf_backing(session);
    for (uint32_t i = 0; i < PROCESS_ELF_BACKING_MAX; i++) {
        if (!g_process_elf_backings[i].used) {
            backing = &g_process_elf_backings[i];
            session->elf_backing_slot = (uint8_t)i;
            break;
        }
    }
    if (backing == 0) {
        return 0;
    }
    backing->used = 1u;
    backing->refs = 1u;
    backing->size = image_size;
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t offset = i * USER_PAGE_SIZE;
        uint32_t chunk = image_size - offset;
        uint8_t *dest;

        if (chunk > USER_PAGE_SIZE) {
            chunk = USER_PAGE_SIZE;
        }
        backing->pages[i] = pmm_alloc_page();
        if (backing->pages[i] == 0) {
            process_release_elf_backing(session);
            return 0;
        }
        dest = (uint8_t *)hal_phys_direct_map(backing->pages[i]);
        memset(dest, 0, USER_PAGE_SIZE);
        memcpy(dest, image + offset, chunk);
    }
    session->elf_image_size = image_size;
    return 1;
}

static int process_copy_from_elf_backing(const struct process_session *session,
                                         uint64_t image_offset,
                                         uint64_t user_addr,
                                         uint64_t size) {
    struct process_elf_backing *backing = process_elf_backing_get(session->elf_backing_slot);

    if (backing == 0 || image_offset + size > backing->size) {
        return 0;
    }
    while (size != 0) {
        uint32_t page_index = (uint32_t)(image_offset / USER_PAGE_SIZE);
        uint32_t page_offset = (uint32_t)(image_offset & (USER_PAGE_SIZE - 1u));
        uint32_t chunk = USER_PAGE_SIZE - page_offset;
        const uint8_t *src;

        if (chunk > size) {
            chunk = (uint32_t)size;
        }
        src = (const uint8_t *)hal_phys_direct_map(backing->pages[page_index]);
        if (!addrspace_copy_to_range(user_addr, src + page_offset, chunk)) {
            return 0;
        }
        image_offset += chunk;
        user_addr += chunk;
        size -= chunk;
    }
    return 1;
}

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

static void process_init_bound_elf_process(struct process *proc) {
    g_bound_session->process = *proc;
    process_forget_files(proc);
    process_discard_non_stdio_files(&g_bound_session->process);
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

static int process_register_elf_segment(uint32_t image_size,
                                        const struct elf64_phdr *phdr) {
    struct process_elf_segment *segment;

    if (phdr->p_offset + phdr->p_filesz > image_size || phdr->p_filesz > phdr->p_memsz) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_BOUNDS;
        return 0;
    }
    if (phdr->p_vaddr < USER_ELF_BASE || phdr->p_vaddr + phdr->p_memsz > USER_ELF_LIMIT) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_ADDR;
        return 0;
    }
    if (g_bound_session->elf_segment_count >= PROCESS_ELF_SEGMENT_MAX) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_BOUNDS;
        return 0;
    }
    segment = &g_bound_session->elf_segments[g_bound_session->elf_segment_count++];
    segment->vaddr = phdr->p_vaddr;
    segment->memsz = phdr->p_memsz;
    segment->filesz = phdr->p_filesz;
    segment->offset = phdr->p_offset;
    segment->perms =
        ((phdr->p_flags & ELF_PF_W) != 0 ? VMM_PERM_WRITE : VMM_PERM_NONE) |
        ((phdr->p_flags & ELF_PF_X) != 0 ? VMM_PERM_EXEC : VMM_PERM_NONE);
    return 1;
}

int process_handle_demand_page_fault(struct process_session *session,
                                     struct user_page_mapping *mappings,
                                     uint64_t fault_addr,
                                     uint64_t error_code) {
    uint64_t page = fault_addr & ~(uint64_t)(USER_PAGE_SIZE - 1u);

    if (session == 0 || mappings == 0 || (error_code & 0x1u) != 0 ||
        session->elf_image_size == 0) {
        return 0;
    }
    for (uint16_t i = 0; i < session->elf_segment_count; i++) {
        const struct process_elf_segment *segment = &session->elf_segments[i];
        uint64_t segment_end = segment->vaddr + segment->memsz;
        uint64_t page_end = page + USER_PAGE_SIZE;
        uint64_t copy_start;
        uint64_t copy_end;

        if (page >= segment_end || page_end <= segment->vaddr) {
            continue;
        }
        process_bind_session(session, mappings);
        if (!vmm_root_is_current(session->address_space.user_cr3) &&
            !vmm_switch_root_or_fail(session->address_space.user_cr3)) {
            return 0;
        }
        if (!addrspace_map_page_at(page, segment->perms)) {
            return 0;
        }
        copy_start = page > segment->vaddr ? page : segment->vaddr;
        copy_end = page_end < segment->vaddr + segment->filesz
            ? page_end
            : segment->vaddr + segment->filesz;
        if (copy_end > copy_start) {
            uint64_t image_offset = segment->offset + (copy_start - segment->vaddr);

            if (image_offset + (copy_end - copy_start) > session->elf_image_size ||
                !process_copy_from_elf_backing(session,
                                               image_offset,
                                               copy_start,
                                               copy_end - copy_start)) {
                (void)addrspace_free_page(page);
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

int process_begin_elf_session(void) {
    const struct process *parent_proc =
        g_user_session.process.image_kind != PROCESS_IMAGE_NONE ? &g_user_session.process : NULL;
    struct process *proc = process_alloc_slot(&g_user_session, parent_proc);

    if (proc == NULL) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ENTER;
        return 0;
    }
    g_user_session.fpu_state_valid = 0u;
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
    if (image_size > NOS_ELF_FILE_BUFFER_SIZE) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_FILE_TOO_LARGE;
        return 0;
    }
    if (!process_create_elf_backing(g_bound_session, image, image_size)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    g_bound_session->elf_segment_count = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf64_phdr *phdr = &phdrs[i];

        if (phdr->p_type != ELF_PT_LOAD || phdr->p_memsz == 0) {
            continue;
        }
        if (!process_register_elf_segment(image_size, phdr)) {
            return 0;
        }
    }

    *entry_out = ehdr->e_entry;
    if (!process_handle_demand_page_fault(g_bound_session,
                                          g_bound_mappings,
                                          ehdr->e_entry,
                                          0x4u)) {
        g_process_exec_last_error = PROCESS_EXEC_ERR_ELF_SEGMENT_MAP;
        return 0;
    }
    return 1;
}
