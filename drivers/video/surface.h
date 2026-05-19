#pragma once

#include <stdint.h>

enum {
    SURFACE_FORMAT_XRGB8888 = 1u
};

struct surface {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
    uint32_t page_count;
    uint64_t phys;
    uint32_t *pixels;
};

int surface_create(struct surface *surface, uint32_t width, uint32_t height);
void surface_destroy(struct surface *surface);
void surface_clear(struct surface *surface, uint32_t rgb);
void surface_draw_pixel(struct surface *surface, int32_t x, int32_t y, uint32_t rgb);
void surface_draw_line(struct surface *surface, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb);
void surface_draw_rect(struct surface *surface, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
void surface_fill_rect(struct surface *surface, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
void surface_draw_triangle(struct surface *surface,
                           int32_t x0,
                           int32_t y0,
                           int32_t x1,
                           int32_t y1,
                           int32_t x2,
                           int32_t y2,
                           uint32_t rgb);
void surface_fill_triangle(struct surface *surface,
                           int32_t x0,
                           int32_t y0,
                           int32_t x1,
                           int32_t y1,
                           int32_t x2,
                           int32_t y2,
                           uint32_t rgb);
void surface_draw_circle(struct surface *surface, int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
void surface_fill_circle(struct surface *surface, int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
void surface_blit(struct surface *dst,
                  const struct surface *src,
                  uint32_t src_x,
                  uint32_t src_y,
                  uint32_t width,
                  uint32_t height,
                  int32_t dst_x,
                  int32_t dst_y);
