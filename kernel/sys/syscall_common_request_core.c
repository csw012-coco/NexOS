#include "abi/syscall_abi.h"
#include "drivers/audio/audio.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/runtime_internal.h"
#include "kernel/internal/core/system_query_internal.h"
#include "kernel/internal/proc/process_program_registry_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/core/profile.h"
#include "kernel/public/mem/pmm.h"
#include "lib/string.h"

extern int input_focus_grab(uint32_t pid) __attribute__((weak));
extern int input_focus_release(uint32_t pid) __attribute__((weak));
extern uint32_t input_focus_owner_pid(void) __attribute__((weak));
extern int kernel_runtime_run_with_irqs_enabled(int (*fn)(void *), void *ctx)
    __attribute__((weak));

static const struct bootx_boot_info *g_common_query_boot_info;
static const struct bootx_memmap_entry *g_common_query_memmap;
static uint32_t g_common_query_memmap_count;
static struct syscall_framebuffer_info g_common_query_fb_info;

void syscall_common_request_core_query_state_init(
    const struct bootx_boot_info *boot_info,
    const struct bootx_memmap_entry *memmap,
    uint32_t memmap_count) {
    g_common_query_boot_info = boot_info;
    g_common_query_memmap = memmap;
    g_common_query_memmap_count = memmap_count;
    syscall_common_request_core_fill_fb_info(boot_info, &g_common_query_fb_info);
}

void syscall_common_request_core_fill_fb_info(
    const struct bootx_boot_info *boot_info,
    struct syscall_framebuffer_info *info) {
    if (info == 0) {
        return;
    }
    memset(info, 0, sizeof(*info));
    if (boot_info == 0) {
        return;
    }
    info->present =
        boot_info->console.type == BOOTX_CONSOLE_FRAMEBUFFER ? 1u : 0u;
    info->type = boot_info->console.type;
    info->addr = boot_info->console.framebuffer_addr;
    info->width = boot_info->console.width;
    info->height = boot_info->console.height;
    info->pitch = boot_info->console.pitch;
    info->bpp = boot_info->console.framebuffer_bpp;
    info->red_mask_size = boot_info->console.red_mask_size;
    info->red_mask_shift = boot_info->console.red_mask_shift;
    info->green_mask_size = boot_info->console.green_mask_size;
    info->green_mask_shift = boot_info->console.green_mask_shift;
    info->blue_mask_size = boot_info->console.blue_mask_size;
    info->blue_mask_shift = boot_info->console.blue_mask_shift;
    info->text_columns = boot_info->console.text_columns;
    info->text_rows = boot_info->console.text_rows;
    info->text_color = boot_info->console.text_color;
}

static void syscall_common_capability_event_sanitize(
    struct syscall_capability_event *event) {
    if (event == 0) {
        return;
    }
    event->source[sizeof(event->source) - 1u] = '\0';
    event->action[sizeof(event->action) - 1u] = '\0';
    event->caps[sizeof(event->caps) - 1u] = '\0';
}

uint64_t syscall_common_request_core_capability_event(
    struct syscall_capability_event *event) {
    if (event == 0) {
        return (uint64_t)-1;
    }
    syscall_common_capability_event_sanitize(event);
    vfs_event_capability_emit(event);
    return 0u;
}

static uint64_t syscall_common_bad_pointer(
    const struct syscall_common_user_input_ops *ops) {
    if (ops != 0 && ops->bad_pointer != 0) {
        return ops->bad_pointer();
    }
    return ops != 0 ? ops->bad_pointer_value : (uint64_t)-1;
}

static uint64_t syscall_common_copy_bad_pointer(
    const struct syscall_common_user_copy_ops *ops) {
    if (ops != 0 && ops->bad_pointer != 0) {
        return ops->bad_pointer();
    }
    return ops != 0 ? ops->bad_pointer_value : (uint64_t)-1;
}

uint64_t syscall_common_request_core_capability_event_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_input_ops *ops) {
    struct syscall_capability_event event;

    if (ops == 0 || ops->copy_from_user == 0 ||
        !ops->copy_from_user(&event, user_info_addr, sizeof(event))) {
        return syscall_common_bad_pointer(ops);
    }
    return syscall_common_request_core_capability_event(&event);
}

int syscall_common_request_core_capability_event_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_input_ops *ops) {
    if (request == 0 || result == 0 ||
        request->number != SYS_CAPABILITY_EVENT) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_capability_event_transfer(
        kernel_syscall_arg_u64(request, 0), ops);
    return 1;
}

static uint64_t syscall_common_misc_call(uint64_t (*fn)(void *), void *ctx,
                                         uint64_t fallback) {
    return fn != 0 ? fn(ctx) : fallback;
}

int syscall_common_request_core_misc_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_misc_ops *ops) {
    if (request == 0 || result == 0 || ops == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_CLEAR:
            result->value = syscall_common_misc_call(ops->clear, ops->ctx, 0u);
            return 1;
        case SYS_TICKS:
            result->value = syscall_common_misc_call(ops->ticks, ops->ctx, 0u);
            return 1;
        case SYS_REBOOT:
            result->value =
                syscall_common_misc_call(ops->reboot, ops->ctx, (uint64_t)-1);
            return 1;
        default:
            return 0;
    }
}

static void syscall_common_query_copy_name(char *dst,
                                           uint32_t dst_size,
                                           const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int syscall_common_query_set_size(uint32_t *info_size, uint32_t size) {
    if (info_size != 0) {
        *info_size = size;
    }
    return 1;
}

int syscall_common_request_core_query_info(uint32_t kind,
                                           uint64_t arg0,
                                           uint64_t arg1,
                                           void *info,
                                           uint32_t *info_size) {
    if (info == 0) {
        return 0;
    }
    if (info_size != 0) {
        *info_size = 0u;
    }
    switch (kind) {
        case SYS_QUERY_BOOT_INFO: {
            struct syscall_boot_info *boot = (struct syscall_boot_info *)info;

            memset(boot, 0, sizeof(*boot));
            if (g_common_query_boot_info != 0) {
                boot->boot_drive = g_common_query_boot_info->boot_drive;
                boot->partition_lba = g_common_query_boot_info->partition_lba;
                boot->partition_sectors =
                    g_common_query_boot_info->partition_sectors;
                boot->module_count = g_common_query_boot_info->module_count;
            }
            return syscall_common_query_set_size(info_size, sizeof(*boot));
        }
        case SYS_QUERY_MEMMAP: {
            struct syscall_memmap_info *memmap =
                (struct syscall_memmap_info *)info;
            uint32_t index = (uint32_t)arg0;

            if (g_common_query_memmap == 0 ||
                index >= g_common_query_memmap_count) {
                return 0;
            }
            memmap->base = g_common_query_memmap[index].base;
            memmap->length = g_common_query_memmap[index].length;
            memmap->type = g_common_query_memmap[index].type;
            memmap->reserved = g_common_query_memmap[index].reserved;
            return syscall_common_query_set_size(info_size, sizeof(*memmap));
        }
        case SYS_QUERY_PMM: {
            struct syscall_pmm_info *pmm = (struct syscall_pmm_info *)info;

            pmm->total_pages = pmm_total_pages();
            pmm->free_pages = pmm_free_pages();
            pmm->used_pages = pmm_used_pages();
            pmm->dropped_pages = pmm_dropped_pages();
            return syscall_common_query_set_size(info_size, sizeof(*pmm));
        }
        case SYS_QUERY_FB:
            *(struct syscall_framebuffer_info *)info = g_common_query_fb_info;
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_framebuffer_info));
        case SYS_QUERY_BLOCK:
            return kernel_query_block_info((uint32_t)arg0,
                                           (struct syscall_block_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_block_info));
        case SYS_QUERY_PART:
            return kernel_query_part_info((uint32_t)arg0,
                                          (uint32_t)arg1,
                                          (struct syscall_partition_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_partition_info));
        case SYS_QUERY_PROGRAM: {
            struct syscall_program_info *program =
                (struct syscall_program_info *)info;
            const char *name = process_program_name((uint32_t)arg0);

            if (name == 0) {
                return 0;
            }
            memset(program, 0, sizeof(*program));
            syscall_common_query_copy_name(program->name,
                                           sizeof(program->name),
                                           name);
            return syscall_common_query_set_size(info_size, sizeof(*program));
        }
        case SYS_QUERY_PCI:
            kernel_query_pci_info((uint32_t)arg0,
                                  (struct syscall_pci_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_pci_info));
        case SYS_QUERY_AC97:
            kernel_query_ac97_info((struct syscall_ac97_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_ac97_info));
        case SYS_QUERY_HDA:
            kernel_query_hda_info((struct syscall_hda_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_hda_info));
        case SYS_QUERY_RTL8139:
            kernel_query_rtl8139_info((struct syscall_rtl8139_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_rtl8139_info));
        case SYS_QUERY_AUDIO:
            (void)kernel_query_audio_info((uint32_t)arg0,
                                          (struct syscall_audio_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_audio_info));
        case SYS_QUERY_RTC:
            return kernel_query_rtc_info((struct syscall_rtc_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_rtc_info));
        case SYS_QUERY_PROFILE:
            if ((arg1 & SYS_PROFILE_QUERY_RESET) != 0u) {
                kernel_profile_reset();
            }
            return kernel_profile_query((uint32_t)arg0,
                                        (struct syscall_profile_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_profile_info));
        default:
            return 0;
    }
}

static int syscall_common_query_kind_supported(uint32_t kind) {
    switch (kind) {
        case SYS_QUERY_BOOT_INFO:
        case SYS_QUERY_MEMMAP:
        case SYS_QUERY_PMM:
        case SYS_QUERY_FB:
        case SYS_QUERY_BLOCK:
        case SYS_QUERY_PART:
        case SYS_QUERY_PROGRAM:
        case SYS_QUERY_PCI:
        case SYS_QUERY_AC97:
        case SYS_QUERY_HDA:
        case SYS_QUERY_RTL8139:
        case SYS_QUERY_AUDIO:
        case SYS_QUERY_RTC:
        case SYS_QUERY_PROFILE:
            return 1;
        default:
            return 0;
    }
}

uint64_t syscall_common_request_core_query_transfer(
    uint32_t kind,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops) {
    union {
        struct syscall_boot_info boot;
        struct syscall_memmap_info memmap;
        struct syscall_pmm_info pmm;
        struct syscall_framebuffer_info fb;
        struct syscall_block_info block;
        struct syscall_partition_info part;
        struct syscall_program_info program;
        struct syscall_pci_info pci;
        struct syscall_ac97_info ac97;
        struct syscall_hda_info hda;
        struct syscall_rtl8139_info rtl8139;
        struct syscall_audio_info audio;
        struct syscall_rtc_info rtc;
        struct syscall_profile_info profile;
    } info;
    uint32_t info_size = 0u;

    if (ops == 0 || ops->copy_to_user == 0) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (!syscall_common_request_core_query_info(
            kind, arg0, arg1, &info, &info_size)) {
        return 0u;
    }
    if (!ops->copy_to_user(user_info_addr, &info, info_size)) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return 1u;
}

int syscall_common_request_core_query_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    uint32_t kind;

    if (request == 0 || result == 0 || request->number != SYS_QUERY) {
        return 0;
    }
    kind = kernel_syscall_arg_u32(request, 0);
    if (!syscall_common_query_kind_supported(kind)) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_query_transfer(
        kind,
        kernel_syscall_arg_u64(request, 1),
        kernel_syscall_arg_u64(request, 2),
        kernel_syscall_arg_u64(request, 3),
        ops);
    return 1;
}

int syscall_common_request_core_gfx_info(uint32_t op,
                                         struct syscall_gfx_info *info) {
    if (op != SYS_GFX_INFO || info == 0) {
        return 0;
    }
    kernel_gfx_info(info);
    return 1;
}

int syscall_common_request_core_gfx_command(
    uint32_t op,
    const struct syscall_gfx_command *cmd) {
    enum kernel_gfx_buffer_kind buffer_kind = kernel_gfx_buffer_kind(op);

    if (buffer_kind != KERNEL_GFX_BUFFER_COMMAND_IN || cmd == 0) {
        return 0;
    }
    return kernel_gfx_dispatch(op, cmd, 0);
}

int syscall_common_request_core_gfx_batch_valid(
    const struct syscall_gfx_batch *batch) {
    if (batch == 0 ||
        batch->count > SYS_GFX_BATCH_MAX_COMMANDS ||
        (batch->flags & ~SYS_GFX_BATCH_PRESENT) != 0u ||
        (batch->count != 0u && batch->entries_addr == 0u) ||
        batch->count > 0xffffffffu / sizeof(struct syscall_gfx_batch_entry)) {
        return 0;
    }
    return 1;
}

int syscall_common_request_core_gfx_batch_dispatch(
    const struct syscall_gfx_batch_entry *entries,
    uint32_t count,
    int allow_blit) {
    if (entries == 0 && count != 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i < count; i++) {
        const struct syscall_gfx_batch_entry *entry = &entries[i];

        if (entry->reserved != 0u ||
            entry->op == SYS_GFX_INFO ||
            entry->op == SYS_GFX_BATCH ||
            entry->op == SYS_GFX_PRESENT ||
            (!allow_blit && entry->op == SYS_GFX_BLIT) ||
            !syscall_common_request_core_gfx_command(entry->op,
                                                     &entry->command)) {
            return 0;
        }
    }
    return 1;
}

int syscall_common_request_core_gfx_blit_plan(
    const struct syscall_gfx_blit *blit,
    uint32_t max_dimension,
    uint64_t max_user_addr,
    struct syscall_common_gfx_blit_plan *plan,
    int *noop) {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t src_x = 0u;
    uint32_t src_y = 0u;
    uint32_t visible_width;
    uint32_t visible_height;
    uint64_t first_addr;
    uint64_t span;
    int32_t dst_x;
    int32_t dst_y;

    if (noop != 0) {
        *noop = 0;
    }
    if (plan != 0) {
        memset(plan, 0, sizeof(*plan));
    }
    if (blit == 0 || plan == 0 ||
        blit->pixels_addr == 0u ||
        blit->width == 0u || blit->height == 0u ||
        blit->width > max_dimension ||
        blit->height > max_dimension ||
        blit->width > 0xffffffffu / sizeof(uint32_t) ||
        blit->pitch < blit->width * sizeof(uint32_t) ||
        blit->format != SYS_GFX_FORMAT_XRGB8888 ||
        blit->flags != 0u ||
        blit->pixels_addr > max_user_addr) {
        return 0;
    }

    kernel_gfx_dimensions(&screen_width, &screen_height);
    if (screen_width == 0u || screen_height == 0u) {
        return 0;
    }
    dst_x = blit->dst_x;
    dst_y = blit->dst_y;
    visible_width = blit->width;
    visible_height = blit->height;
    if (dst_x < 0) {
        uint32_t crop = (uint32_t)(-(int64_t)dst_x);

        if (crop >= visible_width) {
            if (noop != 0) {
                *noop = 1;
            }
            return 1;
        }
        src_x = crop;
        visible_width -= crop;
        dst_x = 0;
    }
    if (dst_y < 0) {
        uint32_t crop = (uint32_t)(-(int64_t)dst_y);

        if (crop >= visible_height) {
            if (noop != 0) {
                *noop = 1;
            }
            return 1;
        }
        src_y = crop;
        visible_height -= crop;
        dst_y = 0;
    }
    if ((uint32_t)dst_x >= screen_width || (uint32_t)dst_y >= screen_height) {
        if (noop != 0) {
            *noop = 1;
        }
        return 1;
    }
    if (visible_width > screen_width - (uint32_t)dst_x) {
        visible_width = screen_width - (uint32_t)dst_x;
    }
    if (visible_height > screen_height - (uint32_t)dst_y) {
        visible_height = screen_height - (uint32_t)dst_y;
    }

    first_addr = blit->pixels_addr +
                 (uint64_t)src_y * blit->pitch +
                 (uint64_t)src_x * sizeof(uint32_t);
    if (first_addr < blit->pixels_addr || first_addr > max_user_addr) {
        return 0;
    }
    span = (uint64_t)(visible_height - 1u) * blit->pitch +
           (uint64_t)visible_width * sizeof(uint32_t);
    if (span == 0u || span > 0xffffffffu ||
        first_addr + span < first_addr ||
        first_addr + span - 1u > max_user_addr) {
        return 0;
    }

    plan->first_addr = first_addr;
    plan->span = span;
    plan->src_x = src_x;
    plan->src_y = src_y;
    plan->visible_width = visible_width;
    plan->visible_height = visible_height;
    plan->dst_x = dst_x;
    plan->dst_y = dst_y;
    plan->pitch = blit->pitch;
    return 1;
}

int syscall_common_request_core_gfx_blit_dispatch(
    const uint32_t *pixels,
    uint32_t pitch,
    uint32_t width,
    uint32_t height,
    int32_t dst_x,
    int32_t dst_y) {
    return kernel_gfx_blit_xrgb8888(pixels, pitch, width, height, dst_x, dst_y);
}

int syscall_common_request_core_clipboard(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (request == 0 || result == 0 || request->number != SYS_CLIPBOARD) {
        return 0;
    }
    if (kernel_syscall_arg_u32(request, 0) != SYS_CLIPBOARD_CLEAR) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_clipboard_set_text("", 0u);
    return 1;
}

uint32_t syscall_common_request_core_clipboard_size(void) {
    return kernel_clipboard_size();
}

const char *syscall_common_request_core_clipboard_text(void) {
    return kernel_clipboard_text();
}

uint32_t syscall_common_request_core_clipboard_copy_size(uint32_t requested) {
    uint32_t size = kernel_clipboard_size();

    return size < requested ? size : requested;
}

uint32_t syscall_common_request_core_clipboard_set_text(const char *text,
                                                       uint32_t bytes) {
    if (text == 0) {
        bytes = 0u;
    }
    if (bytes > KERNEL_CLIPBOARD_TEXT_MAX) {
        bytes = KERNEL_CLIPBOARD_TEXT_MAX;
    }
    return kernel_clipboard_set_text(text == 0 ? "" : text, bytes);
}

uint32_t syscall_common_request_core_clipboard_prepare_get(
    struct syscall_clipboard_transfer *transfer) {
    uint32_t copied;

    if (transfer == 0) {
        return 0u;
    }
    copied = syscall_common_request_core_clipboard_copy_size(transfer->bytes);
    transfer->size = syscall_common_request_core_clipboard_size();
    return copied;
}

uint32_t syscall_common_request_core_clipboard_prepare_set(
    const struct syscall_clipboard_transfer *transfer) {
    if (transfer == 0) {
        return 0u;
    }
    return transfer->bytes < KERNEL_CLIPBOARD_TEXT_MAX
        ? transfer->bytes
        : KERNEL_CLIPBOARD_TEXT_MAX;
}

uint32_t syscall_common_request_core_clipboard_commit_set(
    struct syscall_clipboard_transfer *transfer,
    const char *text,
    uint32_t bytes) {
    uint32_t size;

    size = syscall_common_request_core_clipboard_set_text(text, bytes);
    if (transfer != 0) {
        transfer->size = size;
    }
    return size;
}

uint32_t syscall_common_request_core_clipboard_prepare_size(
    struct syscall_clipboard_transfer *transfer) {
    uint32_t size = syscall_common_request_core_clipboard_size();

    if (transfer != 0) {
        transfer->size = size;
    }
    return size;
}

static uint64_t syscall_common_clipboard_bad_pointer(
    const struct syscall_common_clipboard_transfer_ops *ops) {
    if (ops == 0) {
        return (uint64_t)-1;
    }
    if (ops->bad_pointer != 0) {
        return ops->bad_pointer();
    }
    return ops->bad_pointer_value;
}

uint64_t syscall_common_request_core_clipboard_transfer(
    uint32_t op,
    uint64_t user_info_addr,
    const struct syscall_common_clipboard_transfer_ops *ops,
    char *scratch,
    uint32_t scratch_size) {
    struct syscall_clipboard_transfer transfer;
    uint32_t bytes;

    if (op == SYS_CLIPBOARD_CLEAR) {
        return syscall_common_request_core_clipboard_set_text("", 0u);
    }
    if (ops == 0 || ops->copy_from_user == 0 || ops->copy_to_user == 0 ||
        !ops->copy_from_user(&transfer, user_info_addr, sizeof(transfer))) {
        return syscall_common_clipboard_bad_pointer(ops);
    }

    switch (op) {
        case SYS_CLIPBOARD_GET:
            bytes = syscall_common_request_core_clipboard_prepare_get(
                &transfer);
            if (bytes != 0u &&
                !ops->copy_to_user(transfer.data_addr,
                                   syscall_common_request_core_clipboard_text(),
                                   bytes)) {
                return syscall_common_clipboard_bad_pointer(ops);
            }
            return ops->copy_to_user(user_info_addr, &transfer, sizeof(transfer))
                ? bytes
                : syscall_common_clipboard_bad_pointer(ops);
        case SYS_CLIPBOARD_SET:
            bytes = syscall_common_request_core_clipboard_prepare_set(
                &transfer);
            if (bytes >= scratch_size || scratch == 0) {
                bytes = scratch_size == 0u ? 0u : scratch_size - 1u;
            }
            if (bytes != 0u &&
                !ops->copy_from_user(scratch, transfer.data_addr, bytes)) {
                return syscall_common_clipboard_bad_pointer(ops);
            }
            if (scratch != 0) {
                scratch[bytes] = '\0';
            }
            (void)syscall_common_request_core_clipboard_commit_set(
                &transfer, scratch, bytes);
            return ops->copy_to_user(user_info_addr, &transfer, sizeof(transfer))
                ? transfer.size
                : syscall_common_clipboard_bad_pointer(ops);
        case SYS_CLIPBOARD_SIZE:
            (void)syscall_common_request_core_clipboard_prepare_size(&transfer);
            return ops->copy_to_user(user_info_addr, &transfer, sizeof(transfer))
                ? transfer.size
                : syscall_common_clipboard_bad_pointer(ops);
        default:
            return (uint64_t)-1;
    }
}

struct syscall_common_audio_buffer_call {
    uint32_t index;
    const struct syscall_audio_play_info *info;
    const uint8_t *buffer;
};

struct syscall_common_audio_stream_call {
    uint32_t index;
    struct audio_pcm_stream *stream;
};

static int syscall_common_audio_play_buffer_local(void *ctx) {
    const struct syscall_common_audio_buffer_call *call =
        (const struct syscall_common_audio_buffer_call *)ctx;

    return kernel_audio_play_buffer(call->index, call->info, call->buffer);
}

static int syscall_common_audio_play_stream_local(void *ctx) {
    const struct syscall_common_audio_stream_call *call =
        (const struct syscall_common_audio_stream_call *)ctx;

    return kernel_audio_play_stream(call->index, call->stream);
}

int syscall_common_request_core_audio_play_valid(
    const struct syscall_audio_play_info *info,
    uint32_t max_bytes) {
    if (info == 0 ||
        info->bytes == 0u ||
        info->bytes > max_bytes ||
        (info->flags & ~SYS_AUDIO_PLAY_F_ASYNC) != 0u ||
        info->channels == 0u ||
        info->bits_per_sample == 0u ||
        info->sample_rate == 0u) {
        return 0;
    }
    return 1;
}

uint64_t syscall_common_request_core_audio_play_dispatch(
    uint32_t index,
    const struct syscall_audio_play_info *info,
    const uint8_t *buffer) {
    struct syscall_common_audio_buffer_call call;

    if (info == 0 || buffer == 0) {
        return 0u;
    }
    call.index = index;
    call.info = info;
    call.buffer = buffer;
    if (kernel_runtime_run_with_irqs_enabled != 0) {
        return kernel_runtime_run_with_irqs_enabled(
            syscall_common_audio_play_buffer_local, &call) ? 1u : 0u;
    }
    return syscall_common_audio_play_buffer_local(&call) ? 1u : 0u;
}

uint64_t syscall_common_request_core_audio_play_transfer(
    uint32_t index,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    struct syscall_audio_play_info info;

    if (ops == 0 || ops->copy_from_user == 0 || scratch == 0 ||
        !ops->copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (!syscall_common_request_core_audio_play_valid(&info, scratch_size)) {
        return 0u;
    }
    if (info.data_addr > max_user_addr ||
        !ops->copy_from_user(scratch, info.data_addr, info.bytes)) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return syscall_common_request_core_audio_play_dispatch(
        index, &info, scratch);
}

int syscall_common_request_core_audio_play_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    if (request == 0 || result == 0 || request->number != SYS_AUDIO_PLAY) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_audio_play_transfer(
        kernel_syscall_arg_u32(request, 0),
        kernel_syscall_arg_u64(request, 1),
        ops,
        scratch,
        scratch_size,
        max_user_addr);
    return 1;
}

int syscall_common_request_core_audio_stream_valid(
    const struct syscall_audio_stream_info *info) {
    if (info == 0 ||
        info->data_bytes == 0u ||
        info->channels == 0u ||
        info->bits_per_sample == 0u ||
        info->sample_rate == 0u ||
        (info->flags & ~SYS_AUDIO_PLAY_F_ASYNC) != 0u) {
        return 0;
    }
    return 1;
}

void syscall_common_request_core_audio_stream_init(
    struct audio_pcm_stream *stream,
    const struct syscall_audio_stream_info *info,
    void *ctx,
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes),
    uint32_t (*cancelled)(void *ctx)) {
    if (stream == 0 || info == 0) {
        return;
    }
    stream->sample_rate = info->sample_rate;
    stream->channels = info->channels;
    stream->bits_per_sample = info->bits_per_sample;
    stream->data_bytes = info->data_bytes;
    stream->flags = info->flags;
    stream->ctx = ctx;
    stream->read = read;
    stream->cancelled = cancelled;
}

uint64_t syscall_common_request_core_audio_stream_dispatch(
    uint32_t index,
    struct audio_pcm_stream *stream) {
    struct syscall_common_audio_stream_call call;

    if (stream == 0 || stream->read == 0) {
        return 0u;
    }
    call.index = index;
    call.stream = stream;
    if (kernel_runtime_run_with_irqs_enabled != 0) {
        return kernel_runtime_run_with_irqs_enabled(
            syscall_common_audio_play_stream_local, &call) ? 1u : 0u;
    }
    return syscall_common_audio_play_stream_local(&call) ? 1u : 0u;
}

int syscall_common_request_core_rtl8139_tx_valid(
    const struct syscall_rtl8139_tx_info *info,
    uint32_t max_bytes) {
    if (info == 0 || info->bytes < 14u || info->bytes > max_bytes) {
        return 0;
    }
    return 1;
}

uint64_t syscall_common_request_core_rtl8139_tx_dispatch(
    const uint8_t *frame,
    uint32_t bytes) {
    if (frame == 0 || bytes < 14u) {
        return 0u;
    }
    return kernel_rtl8139_send_frame(frame, bytes) ? 1u : 0u;
}

uint64_t syscall_common_request_core_rtl8139_rx_dispatch(
    struct syscall_rtl8139_rx_info *info) {
    if (info == 0) {
        return 0u;
    }
    return kernel_rtl8139_receive_packet(info) ? 1u : 0u;
}

uint64_t syscall_common_request_core_rtl8139_tx_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    struct syscall_rtl8139_tx_info info;

    if (ops == 0 || ops->copy_from_user == 0 || scratch == 0 ||
        !ops->copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (!syscall_common_request_core_rtl8139_tx_valid(&info, scratch_size)) {
        return 0u;
    }
    if (info.data_addr > max_user_addr ||
        !ops->copy_from_user(scratch, info.data_addr, info.bytes)) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return syscall_common_request_core_rtl8139_tx_dispatch(
        scratch, info.bytes);
}

uint64_t syscall_common_request_core_rtl8139_rx_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops) {
    struct syscall_rtl8139_rx_info info;
    uint64_t rc;

    if (ops == 0 || ops->copy_to_user == 0) {
        return syscall_common_copy_bad_pointer(ops);
    }
    memset(&info, 0, sizeof(info));
    rc = syscall_common_request_core_rtl8139_rx_dispatch(&info);
    if (!ops->copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return rc;
}

int syscall_common_request_core_rtl8139_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_RTL8139_TX_SEND:
            result->value = syscall_common_request_core_rtl8139_tx_transfer(
                kernel_syscall_arg_u64(request, 0),
                ops,
                scratch,
                scratch_size,
                max_user_addr);
            return 1;
        case SYS_RTL8139_RX_DUMP:
            result->value = syscall_common_request_core_rtl8139_rx_transfer(
                kernel_syscall_arg_u64(request, 0), ops);
            return 1;
        default:
            return 0;
    }
}

int syscall_common_request_core_block_read_dispatch(
    uint32_t disk_index,
    uint64_t lba,
    struct syscall_block_read_info *info) {
    if (info == 0) {
        return 0;
    }
    return kernel_block_read(disk_index, lba, info);
}

int syscall_common_request_core_block_write_dispatch(
    uint32_t disk_index,
    uint64_t lba,
    struct syscall_block_write_info *info) {
    if (info == 0) {
        return 0;
    }
    return kernel_block_write(disk_index, lba, info);
}

uint64_t syscall_common_request_core_block_flush_dispatch(uint32_t disk_index) {
    return kernel_block_flush(disk_index) ? 1u : 0u;
}

static void syscall_common_gui_event_from_keyboard(
    struct syscall_gui_event *event,
    const struct keyboard_event_record *record) {
    event->type = SYS_GUI_EVENT_KEY;
    event->seq = record->seq;
    event->tick = record->tick;
    event->dx = 0;
    event->dy = 0;
    event->buttons = 0u;
    event->keycode = (uint32_t)record->event.keycode;
    event->ascii = record->event.ascii;
    event->pressed = record->event.pressed;
    event->released = record->event.released;
    event->shift = record->event.shift;
    event->ctrl = record->event.ctrl;
    event->alt = record->event.alt;
}

static void syscall_common_gui_event_from_mouse(
    struct syscall_gui_event *event,
    const struct mouse_event_record *record) {
    event->type = SYS_GUI_EVENT_MOUSE;
    event->seq = record->seq;
    event->tick = record->tick;
    event->dx = record->dx;
    event->dy = record->dy;
    event->buttons = record->buttons;
    event->keycode = 0u;
    event->ascii = 0;
    event->pressed = 0u;
    event->released = 0u;
    event->shift = 0u;
    event->ctrl = 0u;
    event->alt = 0u;
}

int syscall_common_request_core_gui_event_cursor_init(
    struct syscall_gui_event_cursor *cursor) {
    if (cursor == 0) {
        return 0;
    }
    cursor->keyboard_seq = keyboard_event_queue_latest_seq();
    cursor->mouse_seq = mouse_event_latest_seq();
    return 1;
}

uint64_t syscall_common_request_core_gui_event_poll(
    struct syscall_gui_event_poll *poll,
    uint32_t current_pid) {
    struct keyboard_event_record key_record;
    struct mouse_event_record mouse_record;
    struct syscall_gui_event_cursor key_cursor;
    struct syscall_gui_event_cursor mouse_cursor;
    int have_key;
    int have_mouse;
    int use_key;

    if (poll == 0) {
        return (uint64_t)-1;
    }
    if (input_focus_owner_pid != 0) {
        uint32_t focus_pid = input_focus_owner_pid();

        if (focus_pid != 0u &&
            (current_pid == 0u || focus_pid != current_pid)) {
            poll->event.type = SYS_GUI_EVENT_NONE;
            return SYS_GUI_EVENT_EMPTY;
        }
    }

    key_cursor = poll->cursor;
    mouse_cursor = poll->cursor;
    have_key = keyboard_event_queue_get_after(&key_cursor.keyboard_seq,
                                              &key_record);
    have_mouse = mouse_event_get_after(&mouse_cursor.mouse_seq, &mouse_record);

    poll->event.type = SYS_GUI_EVENT_NONE;
    poll->keyboard_dropped = keyboard_event_queue_dropped();
    poll->mouse_dropped = mouse_event_dropped();
    if (!have_key && !have_mouse) {
        return SYS_GUI_EVENT_EMPTY;
    }

    use_key = have_key && (!have_mouse || key_record.tick <= mouse_record.tick);
    if (use_key) {
        poll->cursor.keyboard_seq = key_cursor.keyboard_seq;
        syscall_common_gui_event_from_keyboard(&poll->event, &key_record);
    } else {
        poll->cursor.mouse_seq = mouse_cursor.mouse_seq;
        syscall_common_gui_event_from_mouse(&poll->event, &mouse_record);
    }
    return SYS_GUI_EVENT_READY;
}

uint64_t syscall_common_request_core_gui_event_grab(uint32_t current_pid,
                                                    int foreground_allowed) {
    if (current_pid == 0u || !foreground_allowed) {
        return (uint64_t)-1;
    }
    if (input_focus_grab == 0) {
        return 0u;
    }
    return input_focus_grab(current_pid) ? 0u : (uint64_t)-1;
}

uint64_t syscall_common_request_core_gui_event_release(uint32_t current_pid) {
    if (current_pid == 0u) {
        return (uint64_t)-1;
    }
    if (input_focus_release == 0) {
        return 0u;
    }
    return input_focus_release(current_pid) ? 0u : (uint64_t)-1;
}

int syscall_common_request_core_gui_event_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    uint32_t current_pid,
    int foreground_allowed) {
    uint32_t op;

    if (request == 0 || result == 0 || request->number != SYS_GUI_EVENT) {
        return 0;
    }
    op = kernel_syscall_arg_u32(request, 0);
    result->action = SYSCALL_RESULT_RETURN;
    switch (op) {
        case SYS_GUI_EVENT_GRAB:
            result->value = syscall_common_request_core_gui_event_grab(
                current_pid, foreground_allowed);
            return 1;
        case SYS_GUI_EVENT_RELEASE:
            result->value =
                syscall_common_request_core_gui_event_release(current_pid);
            return 1;
        default:
            return 0;
    }
}

int syscall_common_request_core_backend(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (syscall_common_request_core_clipboard(request, result)) {
        return 1;
    }
    switch (request->number) {
        case SYS_AUDIO_TONE:
            result->value = kernel_audio_play_tone(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2)) ? 1u : 0u;
            return 1;
        case SYS_RTL8139_TX_TEST:
            result->value = kernel_rtl8139_send_test_frame() ? 1u : 0u;
            return 1;
        default:
            return 0;
    }
}
