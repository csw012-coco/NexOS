#pragma once

#include <stdint.h>

struct hal_interrupt_handlers {
    void (*divide_error)(void);
    void (*double_fault)(void);
    void (*invalid_opcode)(void);
    void (*general_protection_fault)(void);
    void (*page_fault)(void);
    void (*irq0)(void);
    void (*irq1)(void);
    void (*syscall)(void);
};

struct hal_irq_route {
    uint8_t irq;
    uint8_t acpi_override;
    uint16_t flags;
    uint32_t gsi;
};

struct janus_console_info;
struct janus_boot_info;
struct exception_frame;
struct process_context;
struct syscall_frame;
struct surface;

struct hal_boot_trace_ops {
    void (*text)(void *ctx, const char *text);
    void (*hex64)(void *ctx, const char *label, uint64_t value);
};

enum {
    HAL_EXCEPTION_REGISTER_RAX = 0,
    HAL_EXCEPTION_REGISTER_RBX,
    HAL_EXCEPTION_REGISTER_RCX,
    HAL_EXCEPTION_REGISTER_RDX,
    HAL_EXCEPTION_REGISTER_RSI,
    HAL_EXCEPTION_REGISTER_RDI,
    HAL_EXCEPTION_REGISTER_RBP,
    HAL_EXCEPTION_REGISTER_R8,
    HAL_EXCEPTION_REGISTER_R9,
    HAL_EXCEPTION_REGISTER_R10,
    HAL_EXCEPTION_REGISTER_R11,
    HAL_EXCEPTION_REGISTER_R12,
    HAL_EXCEPTION_REGISTER_R13,
    HAL_EXCEPTION_REGISTER_R14,
    HAL_EXCEPTION_REGISTER_R15,
    HAL_EXCEPTION_REGISTER_COUNT,
    HAL_EXCEPTION_RAW_WORD_MAX = 24
};

struct hal_exception_snapshot {
    uint64_t general[HAL_EXCEPTION_REGISTER_COUNT];
    uint64_t control0;
    uint64_t fault_address;
    uint64_t paging_root;
    uint64_t control4;
    uint64_t instruction_pointer;
    uint64_t code_selector;
    uint64_t flags;
    uint64_t stack_pointer;
    uint64_t stack_selector;
    uint64_t error_code;
    uint64_t raw_words[HAL_EXCEPTION_RAW_WORD_MAX];
    uint32_t raw_word_count;
    uint8_t user_mode;
};

enum {
    HAL_TEXT_WIDTH = 320,
    HAL_TEXT_HEIGHT = 90,
    HAL_FPU_STATE_SIZE = 512
};

#if defined(__i386__)
enum {
    HAL_USER_DYNAMIC_PAGE_LIMIT = 1024,
    HAL_USER_ELF_BASE = 0x08000000ull,
    HAL_USER_ELF_LIMIT = 0x50000000ull,
    HAL_USER_ELF_STACK_TOP = 0xc0000000ull,
    HAL_USER_ELF_STACK_SIZE = 0x10000ull,
    HAL_USER_ELF_STACK_INIT_OFFSET = 4ull,
    HAL_USER_MMAP_BASE = 0x51000000ull,
    HAL_USER_MMAP_END = 0x70000000ull,
    HAL_USER_ALLOC_BASE = 0x50000000ull,
    HAL_USER_ALLOC_END = 0x70000000ull
};
#else
enum {
    HAL_USER_DYNAMIC_PAGE_LIMIT = 2048,
    HAL_USER_ELF_BASE = 0x0000008000000000ull,
    HAL_USER_ELF_LIMIT = 0x0000008000400000ull,
    HAL_USER_ELF_STACK_TOP = 0x0000008000800000ull,
    HAL_USER_ELF_STACK_SIZE = 0x10000ull,
    HAL_USER_ELF_STACK_INIT_OFFSET = 8ull,
    HAL_USER_MMAP_BASE = 0x0000008001000000ull,
    HAL_USER_MMAP_END = 0x0000008001800000ull,
    HAL_USER_ALLOC_BASE = 0x0000008000800000ull,
    HAL_USER_ALLOC_END = 0x0000008001000000ull
};
#endif

#define HAL_DISPLAY_CELL_CODEPOINT_MASK 0x001fffffu
#define HAL_DISPLAY_CELL_WIDE 0x00200000u
#define HAL_DISPLAY_CELL_CONT 0x00400000u
#define HAL_DISPLAY_CELL_FLAGS_MASK 0x00e00000u
#define HAL_DISPLAY_CELL_COLOR_SHIFT 24u

void hal_paging_init(uint64_t kernel_phys_addr);
int hal_pmm_init_from_boot(const struct janus_boot_info *boot_info,
                           uint64_t kernel_phys_addr);
void hal_display_load_font(const struct janus_boot_info *boot_info);
void hal_display_init(const struct janus_console_info *console);
int hal_display_enable_backbuffer(void);
void hal_display_begin_update(void);
void hal_display_end_update(void);
void hal_display_service_pending(void);
void hal_platform_init(const struct hal_interrupt_handlers *handlers);
int hal_paging_enabled(void);
uint64_t hal_paging_current_root(void);
void hal_paging_switch_root(uint64_t cr3);
uint64_t hal_paging_create_user_root(void);
uint64_t hal_paging_clone_root_cow(uint64_t source_cr3);
int hal_paging_resolve_cow_fault(uint64_t root_cr3, uint64_t fault_addr, uint64_t error_code);
void hal_paging_destroy_user_root(uint64_t cr3);
void hal_paging_allow_user_page(uint64_t addr);
void hal_paging_allow_user_range(uint64_t start, uint64_t end);
void hal_paging_set_supervisor_range(uint64_t start, uint64_t end);
int hal_paging_map_page(uint64_t virt_addr, uint64_t phys_addr, int user_accessible, int writable);
int hal_paging_guard_kernel_page(uint64_t virt_addr);
int hal_paging_map_page_with_exec(uint64_t virt_addr,
                                  uint64_t phys_addr,
                                  int user_accessible,
                                  int writable,
                                  int executable);
int hal_paging_set_write_combining(uint64_t virt_addr, uint64_t size);
int hal_paging_unmap_page(uint64_t virt_addr, uint64_t *phys_addr);
int hal_paging_get_mapping(uint64_t virt_addr, uint64_t *phys_addr);
int hal_paging_get_mapping_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags);
int hal_paging_get_mapping_info_in_root(uint64_t root,
                                        uint64_t virt_addr,
                                        uint64_t *phys_addr,
                                        uint64_t *flags);
void hal_paging_log_init_exec_failure(const struct hal_boot_trace_ops *ops, void *ctx);
void hal_paging_log_panic_entry(const struct hal_boot_trace_ops *ops,
                                void *ctx,
                                uint64_t entry);
void hal_paging_log_panic_target_entry(const struct hal_boot_trace_ops *ops,
                                       void *ctx,
                                       uint64_t target_root,
                                       uint64_t entry);
void hal_paging_log_panic_switch_trace(const struct hal_boot_trace_ops *ops,
                                       void *ctx,
                                       uint64_t entry);
void hal_paging_log_panic_summary(const struct hal_boot_trace_ops *ops,
                                  void *ctx,
                                  uint64_t target_root,
                                  uint64_t entry);
int hal_process_context_init_user(struct process_context *context,
                                  uint64_t entry,
                                  uint64_t stack,
                                  uint64_t first_argument,
                                  int user_mode);
void *hal_phys_direct_map(uint64_t phys_addr);
int hal_phys_temporary_map(uint64_t phys_addr, uint32_t slot, void **virt_out);
void hal_phys_temporary_unmap(uint32_t slot);
void *hal_mmio_map(uint64_t phys_addr, uint64_t length);
void hal_timer_init(uint32_t pit_hz);
void hal_timer_notify_tick(void);
uint32_t hal_timer_current_ticks(void);
uint32_t hal_timer_hz(void);
void hal_irq_ack(uint8_t irq);
void hal_irq_set_mask(uint8_t irq, int masked);
int hal_irq_route(uint8_t irq, struct hal_irq_route *out);
uint8_t hal_keyboard_read_scancode(void);
int hal_keyboard_inject_scancode(uint8_t scancode);
uint32_t hal_display_read_cell(uint16_t row, uint16_t col);
void hal_display_write_cell(uint16_t row, uint16_t col, uint32_t value);
void hal_display_clear_row(uint16_t row, uint8_t color);
void hal_display_put_at(uint16_t row, uint16_t col, uint8_t color, char ch);
void hal_display_enable_cursor(uint8_t start, uint8_t end);
void hal_display_disable_cursor(void);
void hal_display_set_cursor(uint16_t row, uint16_t col);
uint16_t hal_display_text_columns(void);
uint16_t hal_display_text_rows(void);
uint32_t hal_display_cell_height(void);
void hal_display_bitblt(uint32_t src_x,
                        uint32_t src_y,
                        uint32_t width,
                        uint32_t height,
                        uint32_t dst_x,
                        uint32_t dst_y);
void hal_display_scroll_rows(uint16_t top_row, uint16_t bottom_row, uint8_t clear_color);
void hal_display_blit_surface(const struct surface *surface,
                              uint32_t src_x,
                              uint32_t src_y,
                              uint32_t width,
                              uint32_t height,
                              int32_t dst_x,
                              int32_t dst_y);
void hal_display_blit_xrgb8888(const uint32_t *pixels,
                               uint32_t pitch,
                               uint32_t width,
                               uint32_t height,
                               int32_t dst_x,
                               int32_t dst_y);
void hal_display_draw_pixel(int32_t x, int32_t y, uint32_t rgb);
void hal_display_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb);
void hal_display_draw_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
void hal_display_fill_rect_rgb(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
void hal_display_draw_triangle(int32_t x0,
                               int32_t y0,
                               int32_t x1,
                               int32_t y1,
                               int32_t x2,
                               int32_t y2,
                               uint32_t rgb);
void hal_display_fill_triangle(int32_t x0,
                               int32_t y0,
                               int32_t x1,
                               int32_t y1,
                               int32_t x2,
                               int32_t y2,
                               uint32_t rgb);
void hal_display_draw_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
void hal_display_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
void hal_display_present(void);
void hal_display_set_mouse_cursor_enabled(int enabled);
void hal_display_move_mouse_cursor(int32_t dx, int32_t dy);
int hal_display_mouse_cursor_cell(uint16_t *row_out, uint16_t *col_out);
uint8_t hal_io_in8(uint16_t port);
uint16_t hal_io_in16(uint16_t port);
void hal_io_out8(uint16_t port, uint8_t value);
void hal_io_out16(uint16_t port, uint16_t value);
void hal_cpu_cli(void);
void hal_cpu_sti(void);
void hal_cpu_halt(void);
void hal_cpu_wait_for_interrupt(void);
void hal_cpu_wait_for_event(void);
void hal_cpu_relax(void);
void hal_cpu_enable_sse(void);
void hal_cpu_trigger_triple_fault(void);
void hal_fpu_state_init(void *state);
void hal_fpu_state_save(void *state);
void hal_fpu_state_restore(const void *state);
uint64_t hal_cpu_current_sp(void);
uint64_t hal_cpu_read_tsc(void);
void hal_cpu_cpuid(uint32_t leaf,
                   uint32_t subleaf,
                   uint32_t *eax,
                   uint32_t *ebx,
                   uint32_t *ecx,
                   uint32_t *edx);
const char *hal_arch_name(void);
void hal_usermode_enter(uint64_t entry, uint64_t user_stack);
void hal_usermode_resume(const struct syscall_frame *frame);
uint64_t hal_kernel_stack_top(void);
void hal_set_kernel_stack_top(uint64_t rsp0);
uint64_t hal_kernel_rsp0_guard_address(void);
uint64_t hal_double_fault_guard_address(void);
int hal_exception_frame_is_user(const struct exception_frame *frame);
uint64_t hal_exception_frame_ip(const struct exception_frame *frame);
uint64_t hal_exception_frame_error_code(const struct exception_frame *frame);
uint64_t hal_page_fault_address(void);
void hal_exception_snapshot(const struct exception_frame *frame,
                            struct hal_exception_snapshot *snapshot);
int hal_syscall_frame_is_user(const struct syscall_frame *frame);
uint64_t hal_syscall_frame_ip(const struct syscall_frame *frame);
uint64_t hal_syscall_frame_sp(const struct syscall_frame *frame);
