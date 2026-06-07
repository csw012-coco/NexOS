#include "kernel/internal/core/graphics_service_internal.h"

#include "bootx/bootx.h"
#include "hal/hal.h"

static uint32_t g_kernel_gfx_width;
static uint32_t g_kernel_gfx_height;
static uint32_t g_kernel_gfx_pitch;
static uint32_t g_kernel_gfx_bpp;

void kernel_gfx_init(const struct bootx_console_info *console) {
    if (console == 0 || console->type != BOOTX_CONSOLE_FRAMEBUFFER) {
        g_kernel_gfx_width = 0u;
        g_kernel_gfx_height = 0u;
        g_kernel_gfx_pitch = 0u;
        g_kernel_gfx_bpp = 0u;
        return;
    }

    g_kernel_gfx_width = console->width;
    g_kernel_gfx_height = console->height;
    g_kernel_gfx_pitch = console->pitch;
    g_kernel_gfx_bpp = console->framebuffer_bpp;
}

static void kernel_gfx_fill_info(struct syscall_gfx_info *info) {
    if (info == 0) {
        return;
    }

    info->width = g_kernel_gfx_width;
    info->height = g_kernel_gfx_height;
    info->pitch = g_kernel_gfx_pitch;
    info->bpp = g_kernel_gfx_bpp;
    info->text_columns = hal_display_text_columns();
    info->text_rows = hal_display_text_rows();
}

enum kernel_gfx_buffer_kind kernel_gfx_buffer_kind(uint32_t op) {
    switch (op) {
        case SYS_GFX_INFO:
            return KERNEL_GFX_BUFFER_INFO_OUT;
        case SYS_GFX_CLEAR:
        case SYS_GFX_PIXEL:
        case SYS_GFX_LINE:
        case SYS_GFX_RECT:
        case SYS_GFX_FILL_RECT:
        case SYS_GFX_TRIANGLE:
        case SYS_GFX_FILL_TRIANGLE:
        case SYS_GFX_CIRCLE:
        case SYS_GFX_FILL_CIRCLE:
        case SYS_GFX_PRESENT:
            return KERNEL_GFX_BUFFER_COMMAND_IN;
        default:
            return KERNEL_GFX_BUFFER_INVALID;
    }
}

int kernel_gfx_dispatch(uint32_t op, const struct syscall_gfx_command *cmd, struct syscall_gfx_info *info) {
    if (op == SYS_GFX_INFO) {
        kernel_gfx_fill_info(info);
        return 1;
    }
    if (cmd == 0 || g_kernel_gfx_width == 0u || g_kernel_gfx_height == 0u) {
        return 0;
    }

    switch (op) {
        case SYS_GFX_CLEAR:
            hal_display_fill_rect_rgb(0, 0, g_kernel_gfx_width, g_kernel_gfx_height, cmd->rgb);
            return 1;
        case SYS_GFX_PIXEL:
            hal_display_draw_pixel(cmd->x0, cmd->y0, cmd->rgb);
            return 1;
        case SYS_GFX_LINE:
            hal_display_draw_line(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->rgb);
            return 1;
        case SYS_GFX_RECT:
            hal_display_draw_rect(cmd->x0, cmd->y0, cmd->width, cmd->height, cmd->rgb);
            return 1;
        case SYS_GFX_FILL_RECT:
            hal_display_fill_rect_rgb(cmd->x0, cmd->y0, cmd->width, cmd->height, cmd->rgb);
            return 1;
        case SYS_GFX_TRIANGLE:
            hal_display_draw_triangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->rgb);
            return 1;
        case SYS_GFX_FILL_TRIANGLE:
            hal_display_fill_triangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->rgb);
            return 1;
        case SYS_GFX_CIRCLE:
            hal_display_draw_circle(cmd->x0, cmd->y0, cmd->radius, cmd->rgb);
            return 1;
        case SYS_GFX_FILL_CIRCLE:
            hal_display_fill_circle(cmd->x0, cmd->y0, cmd->radius, cmd->rgb);
            return 1;
        case SYS_GFX_PRESENT:
            hal_display_present();
            return 1;
        default:
            return 0;
    }
}

void kernel_gfx_begin_batch(void) {
    hal_display_begin_update();
}

void kernel_gfx_end_batch(uint32_t flags) {
    if ((flags & SYS_GFX_BATCH_PRESENT) != 0u) {
        hal_display_present();
    }
    hal_display_end_update();
}
