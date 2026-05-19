#include "drivers/video/surface.h"

#include "hal/hal.h"
#include "kernel/public/mem/pmm.h"
#include "lib/string.h"

enum {
    SURFACE_PAGE_SIZE = 4096u
};

static uint32_t surface_pack_rgb(uint32_t rgb) {
    return 0xff000000u | (rgb & 0x00ffffffu);
}

static int32_t surface_abs_i32(int32_t value) {
    return value < 0 ? -value : value;
}

static int32_t surface_min_i32(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int32_t surface_max_i32(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static int surface_valid(const struct surface *surface) {
    return surface != 0 &&
           surface->pixels != 0 &&
           surface->format == SURFACE_FORMAT_XRGB8888 &&
           surface->width != 0u &&
           surface->height != 0u &&
           surface->pitch >= surface->width * 4u;
}

static void surface_put_pixel_raw(struct surface *surface, int32_t x, int32_t y, uint32_t color) {
    uint32_t *row;

    if (!surface_valid(surface) || x < 0 || y < 0 ||
        x >= (int32_t)surface->width || y >= (int32_t)surface->height) {
        return;
    }
    row = (uint32_t *)((uint8_t *)surface->pixels + (uint32_t)y * surface->pitch);
    row[x] = color;
}

static void surface_draw_line_raw(struct surface *surface,
                                  int32_t x0,
                                  int32_t y0,
                                  int32_t x1,
                                  int32_t y1,
                                  uint32_t color) {
    int32_t dx;
    int32_t dy;
    int32_t sx;
    int32_t sy;
    int32_t err;

    if (!surface_valid(surface)) {
        return;
    }
    dx = surface_abs_i32(x1 - x0);
    dy = -surface_abs_i32(y1 - y0);
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        int32_t e2;

        surface_put_pixel_raw(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int64_t surface_triangle_edge(int32_t ax,
                                     int32_t ay,
                                     int32_t bx,
                                     int32_t by,
                                     int32_t px,
                                     int32_t py) {
    return (int64_t)(px - ax) * (int64_t)(by - ay) -
           (int64_t)(py - ay) * (int64_t)(bx - ax);
}

static void surface_draw_circle_points(struct surface *surface,
                                       int32_t cx,
                                       int32_t cy,
                                       int32_t x,
                                       int32_t y,
                                       uint32_t color) {
    surface_put_pixel_raw(surface, cx + x, cy + y, color);
    surface_put_pixel_raw(surface, cx - x, cy + y, color);
    surface_put_pixel_raw(surface, cx + x, cy - y, color);
    surface_put_pixel_raw(surface, cx - x, cy - y, color);
    surface_put_pixel_raw(surface, cx + y, cy + x, color);
    surface_put_pixel_raw(surface, cx - y, cy + x, color);
    surface_put_pixel_raw(surface, cx + y, cy - x, color);
    surface_put_pixel_raw(surface, cx - y, cy - x, color);
}

int surface_create(struct surface *surface, uint32_t width, uint32_t height) {
    uint64_t bytes;
    uint32_t pages;
    uint64_t phys;
    uint32_t *pixels;

    if (surface == 0 || width == 0u || height == 0u ||
        width > 8192u || height > 8192u ||
        width > 0xffffffffu / 4u) {
        return 0;
    }
    bytes = (uint64_t)width * 4ull * (uint64_t)height;
    if (bytes == 0u || bytes > 0xffffffffull) {
        return 0;
    }
    pages = (uint32_t)((bytes + SURFACE_PAGE_SIZE - 1u) / SURFACE_PAGE_SIZE);
    phys = pmm_alloc_contiguous(pages);
    if (phys == 0u) {
        return 0;
    }
    pixels = (uint32_t *)hal_phys_direct_map(phys);
    if (pixels == 0) {
        for (uint32_t i = 0; i < pages; i++) {
            (void)pmm_free_page(phys + (uint64_t)i * SURFACE_PAGE_SIZE);
        }
        return 0;
    }
    surface->width = width;
    surface->height = height;
    surface->pitch = width * 4u;
    surface->format = SURFACE_FORMAT_XRGB8888;
    surface->page_count = pages;
    surface->phys = phys;
    surface->pixels = pixels;
    memset(surface->pixels, 0, (uint32_t)bytes);
    return 1;
}

void surface_destroy(struct surface *surface) {
    if (surface == 0) {
        return;
    }
    if (surface->phys != 0u && surface->page_count != 0u) {
        for (uint32_t i = 0; i < surface->page_count; i++) {
            (void)pmm_free_page(surface->phys + (uint64_t)i * SURFACE_PAGE_SIZE);
        }
    }
    memset(surface, 0, sizeof(*surface));
}

void surface_clear(struct surface *surface, uint32_t rgb) {
    uint32_t color = surface_pack_rgb(rgb);

    if (!surface_valid(surface)) {
        return;
    }
    for (uint32_t y = 0; y < surface->height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels + y * surface->pitch);

        for (uint32_t x = 0; x < surface->width; x++) {
            row[x] = color;
        }
    }
}

void surface_draw_pixel(struct surface *surface, int32_t x, int32_t y, uint32_t rgb) {
    surface_put_pixel_raw(surface, x, y, surface_pack_rgb(rgb));
}

void surface_draw_line(struct surface *surface, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb) {
    surface_draw_line_raw(surface, x0, y0, x1, y1, surface_pack_rgb(rgb));
}

void surface_fill_rect(struct surface *surface, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    uint32_t color;

    if (!surface_valid(surface) || width == 0u || height == 0u ||
        x >= (int32_t)surface->width || y >= (int32_t)surface->height) {
        return;
    }
    if (x < 0) {
        uint32_t crop = (uint32_t)(-x);

        if (crop >= width) {
            return;
        }
        width -= crop;
        x = 0;
    }
    if (y < 0) {
        uint32_t crop = (uint32_t)(-y);

        if (crop >= height) {
            return;
        }
        height -= crop;
        y = 0;
    }
    if ((uint32_t)x + width > surface->width) {
        width = surface->width - (uint32_t)x;
    }
    if ((uint32_t)y + height > surface->height) {
        height = surface->height - (uint32_t)y;
    }
    color = surface_pack_rgb(rgb);
    for (uint32_t row_index = 0; row_index < height; row_index++) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                     ((uint32_t)y + row_index) * surface->pitch);

        for (uint32_t col = 0; col < width; col++) {
            row[(uint32_t)x + col] = color;
        }
    }
}

void surface_draw_rect(struct surface *surface, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb) {
    int32_t x1;
    int32_t y1;
    uint32_t color;

    if (!surface_valid(surface) || width == 0u || height == 0u) {
        return;
    }
    x1 = x + (int32_t)width - 1;
    y1 = y + (int32_t)height - 1;
    color = surface_pack_rgb(rgb);
    surface_draw_line_raw(surface, x, y, x1, y, color);
    if (height > 1u) {
        surface_draw_line_raw(surface, x, y1, x1, y1, color);
    }
    if (height > 2u) {
        surface_draw_line_raw(surface, x, y + 1, x, y1 - 1, color);
        if (width > 1u) {
            surface_draw_line_raw(surface, x1, y + 1, x1, y1 - 1, color);
        }
    }
}

void surface_draw_triangle(struct surface *surface,
                           int32_t x0,
                           int32_t y0,
                           int32_t x1,
                           int32_t y1,
                           int32_t x2,
                           int32_t y2,
                           uint32_t rgb) {
    uint32_t color = surface_pack_rgb(rgb);

    surface_draw_line_raw(surface, x0, y0, x1, y1, color);
    surface_draw_line_raw(surface, x1, y1, x2, y2, color);
    surface_draw_line_raw(surface, x2, y2, x0, y0, color);
}

void surface_fill_triangle(struct surface *surface,
                           int32_t x0,
                           int32_t y0,
                           int32_t x1,
                           int32_t y1,
                           int32_t x2,
                           int32_t y2,
                           uint32_t rgb) {
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    int64_t area;
    uint32_t color;

    if (!surface_valid(surface)) {
        return;
    }
    min_x = surface_min_i32(surface_min_i32(x0, x1), x2);
    min_y = surface_min_i32(surface_min_i32(y0, y1), y2);
    max_x = surface_max_i32(surface_max_i32(x0, x1), x2);
    max_y = surface_max_i32(surface_max_i32(y0, y1), y2);
    if (max_x < 0 || max_y < 0 || min_x >= (int32_t)surface->width || min_y >= (int32_t)surface->height) {
        return;
    }
    if (min_x < 0) {
        min_x = 0;
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_x >= (int32_t)surface->width) {
        max_x = (int32_t)surface->width - 1;
    }
    if (max_y >= (int32_t)surface->height) {
        max_y = (int32_t)surface->height - 1;
    }
    area = surface_triangle_edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        surface_draw_triangle(surface, x0, y0, x1, y1, x2, y2, rgb);
        return;
    }
    color = surface_pack_rgb(rgb);
    for (int32_t y = min_y; y <= max_y; y++) {
        for (int32_t x = min_x; x <= max_x; x++) {
            int64_t w0 = surface_triangle_edge(x1, y1, x2, y2, x, y);
            int64_t w1 = surface_triangle_edge(x2, y2, x0, y0, x, y);
            int64_t w2 = surface_triangle_edge(x0, y0, x1, y1, x, y);

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                surface_put_pixel_raw(surface, x, y, color);
            }
        }
    }
}

void surface_draw_circle(struct surface *surface, int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    int32_t x = (int32_t)radius;
    int32_t y = 0;
    int32_t err = 0;
    uint32_t color = surface_pack_rgb(rgb);

    if (!surface_valid(surface)) {
        return;
    }
    while (x >= y) {
        y++;
        surface_draw_circle_points(surface, cx, cy, x, y - 1, color);
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void surface_fill_circle(struct surface *surface, int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb) {
    int32_t r = (int32_t)radius;
    int32_t rr = r * r;
    uint32_t color = surface_pack_rgb(rgb);

    if (!surface_valid(surface)) {
        return;
    }
    for (int32_t y = -r; y <= r; y++) {
        for (int32_t x = -r; x <= r; x++) {
            if (x * x + y * y <= rr) {
                surface_put_pixel_raw(surface, cx + x, cy + y, color);
            }
        }
    }
}

void surface_blit(struct surface *dst,
                  const struct surface *src,
                  uint32_t src_x,
                  uint32_t src_y,
                  uint32_t width,
                  uint32_t height,
                  int32_t dst_x,
                  int32_t dst_y) {
    if (!surface_valid(dst) || !surface_valid(src) || width == 0u || height == 0u ||
        src_x >= src->width || src_y >= src->height ||
        dst_x >= (int32_t)dst->width || dst_y >= (int32_t)dst->height) {
        return;
    }
    if (dst_x < 0) {
        uint32_t crop = (uint32_t)(-dst_x);

        if (crop >= width) {
            return;
        }
        width -= crop;
        src_x += crop;
        dst_x = 0;
    }
    if (dst_y < 0) {
        uint32_t crop = (uint32_t)(-dst_y);

        if (crop >= height) {
            return;
        }
        height -= crop;
        src_y += crop;
        dst_y = 0;
    }
    if (src_x + width > src->width) {
        width = src->width - src_x;
    }
    if (src_y + height > src->height) {
        height = src->height - src_y;
    }
    if ((uint32_t)dst_x + width > dst->width) {
        width = dst->width - (uint32_t)dst_x;
    }
    if ((uint32_t)dst_y + height > dst->height) {
        height = dst->height - (uint32_t)dst_y;
    }
    for (uint32_t row_index = 0; row_index < height; row_index++) {
        const void *src_row = (const uint8_t *)src->pixels + (src_y + row_index) * src->pitch + src_x * 4u;
        void *dst_row = (uint8_t *)dst->pixels + ((uint32_t)dst_y + row_index) * dst->pitch + (uint32_t)dst_x * 4u;

        memmove(dst_row, src_row, width * 4u);
    }
}
