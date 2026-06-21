#pragma once

typedef enum ncc_color {
    NCC_COLOR_RED = 3,
    NCC_COLOR_GREEN,
    NCC_COLOR_BLUE = NCC_COLOR_RED
} ncc_color_t;

typedef struct ncc_pair {
    int left;
    int right;
} ncc_pair_t;

typedef ncc_pair_t *ncc_pair_ptr_t;
typedef char ncc_name_t[8];
