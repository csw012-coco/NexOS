#pragma once

#include "hal/hal.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/internal/proc/process_types_internal.h"

enum {
    USER_PAGE_SIZE = NOS_PAGE_SIZE,
    USER_DYNAMIC_PAGE_LIMIT = 512,
    USER_PROCESS_LIMIT = NOS_PROCESS_SLOT_MAX,
    USER_ELF_ARG_MAX = 8,
    USER_ELF_ENV_MAX = 16,
    USER_ALLOC_BASE = 0xffffffff80800000ull,
    USER_ALLOC_END = 0xffffffff81000000ull,
    USER_ELF_BASE = 0x0000008000000000ull,
    USER_ELF_LIMIT = 0x0000008000400000ull,
    USER_ELF_STACK_TOP = 0x0000008000800000ull,
    USER_ELF_STACK_SIZE = 0x4000ull,
    USER_ELF_STACK_BOTTOM = USER_ELF_STACK_TOP - USER_ELF_STACK_SIZE,
    USER_ELF_STACK_INIT = USER_ELF_STACK_TOP - 8ull,
    ELF_ET_EXEC = 2,
    ELF_EM_X86_64 = 62,
    ELF_CLASS_64 = 2,
    ELF_DATA_LSB = 1,
    ELF_PT_LOAD = 1,
    ELF_PF_X = 1,
    ELF_PF_W = 2
};

enum {
    PROCESS_EXEC_OK = 0,
    PROCESS_EXEC_ERR_BAD_ARGS = 1,
    PROCESS_EXEC_ERR_FILE_NOT_FOUND = 2,
    PROCESS_EXEC_ERR_FILE_TOO_LARGE = 3,
    PROCESS_EXEC_ERR_FILE_READ = 4,
    PROCESS_EXEC_ERR_ELF_HEADER = 5,
    PROCESS_EXEC_ERR_ELF_SEGMENT_BOUNDS = 6,
    PROCESS_EXEC_ERR_ELF_SEGMENT_ADDR = 7,
    PROCESS_EXEC_ERR_ELF_SEGMENT_MAP = 8,
    PROCESS_EXEC_ERR_STACK_ALLOC = 9,
    PROCESS_EXEC_ERR_ENTER = 10
};

struct elf64_ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

struct user_page_mapping {
    uint64_t virt_addr;
    uint64_t phys_addr;
    uint8_t used;
    uint8_t reserved_pool;
};

struct process_session {
    struct address_space address_space;
    struct process process;
};

struct job_runtime {
    uint8_t used;
    uint64_t entry;
    uint64_t stack_top;
    struct process_session session;
    struct user_page_mapping mappings[USER_DYNAMIC_PAGE_LIMIT];
};

struct process_program {
    const char *name;
    const char *image_name;
};

struct process_exec_request {
    struct vfs *vfs;
    const char *name;
    const char *const *envp;
    enum process_exec_mode mode;
};

extern struct tty *g_user_tty;
extern volatile uint32_t *g_user_timer_ticks;
extern uint64_t g_current_user_raw_entry;
extern uint8_t g_elf_file_buffer[NOS_ELF_FILE_BUFFER_SIZE];

extern char __kernel_start[];
extern char __kernel_end[];
extern char __userelf_start[];
extern char __userelf_end[];

extern struct user_page_mapping g_user_page_mappings[USER_DYNAMIC_PAGE_LIMIT];
extern struct process_session g_user_session;
extern struct process_session *g_bound_session;
extern struct user_page_mapping *g_bound_mappings;

enum {
    USER_CPU_COUNT = 1
};

struct cpu_user_state {
    uint8_t nested_kernel_stacks[USER_PROCESS_LIMIT][NOS_KERNEL_STACK_SIZE] __attribute__((aligned(16)));
    uint32_t nested_kernel_stack_depth;
    struct process_session *active_sessions[USER_PROCESS_LIMIT];
    struct user_page_mapping *active_mappings[USER_PROCESS_LIMIT];
};

extern struct cpu_user_state g_cpu_user_state[USER_CPU_COUNT];

static inline struct cpu_user_state *current_cpu_user_state(void) {
    return &g_cpu_user_state[0];
}

extern struct process g_process_slots[USER_PROCESS_LIMIT];
extern uint8_t g_process_slot_used[USER_PROCESS_LIMIT];
extern uint32_t g_next_pid;
extern uint64_t g_next_user_alloc;
extern uint32_t g_process_exec_last_error;
extern uint32_t g_process_exec_last_stage;
extern uint32_t g_process_exec_read_open_rc;
extern uint32_t g_process_exec_read_node_kind;
extern uint32_t g_process_exec_read_mount_kind;
extern uint32_t g_process_exec_read_file_size;
extern uint32_t g_process_exec_read_bytes;
extern uint32_t g_process_exec_read_result;
extern struct process g_last_exited_process;
extern struct job_runtime g_bg_runtimes[USER_PROCESS_LIMIT];
extern uint32_t g_scheduler_next_slot;

void process_bind_session(struct process_session *session,
                          struct user_page_mapping *mappings);
