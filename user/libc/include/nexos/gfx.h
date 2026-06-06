#pragma once

#include "user/public/sysapi.h"

int gfx_info(struct syscall_gfx_info *info);
int gfx_clear(uint32_t rgb);
int gfx_draw_pixel(int32_t x, int32_t y, uint32_t rgb);
int gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb);
int gfx_draw_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
int gfx_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t rgb);
int gfx_draw_triangle(int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      int32_t x2,
                      int32_t y2,
                      uint32_t rgb);
int gfx_fill_triangle(int32_t x0,
                      int32_t y0,
                      int32_t x1,
                      int32_t y1,
                      int32_t x2,
                      int32_t y2,
                      uint32_t rgb);
int gfx_draw_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
int gfx_fill_circle(int32_t cx, int32_t cy, uint32_t radius, uint32_t rgb);
int gfx_present(void);
