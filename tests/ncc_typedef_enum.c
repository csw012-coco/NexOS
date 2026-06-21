#include <stdint.h>
#include "ncc_typedef_enum.h"

int main(void) {
    uint8_t byte;
    ncc_color_t color;
    ncc_pair_t pair;
    ncc_pair_ptr_t pointer;
    ncc_name_t name;

    byte = 7;
    color = NCC_COLOR_GREEN;
    pair.left = byte;
    pair.right = color;
    pointer = &pair;
    name[0] = 'T';
    name[1] = '\0';
    printf("%s %d %d %d\n",
           name,
           pointer->left,
           pointer->right,
           NCC_COLOR_BLUE);
    return 0;
}
