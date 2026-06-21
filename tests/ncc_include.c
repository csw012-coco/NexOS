#include <stdio.h>
#include "ncc_include.h"

int ncc_include_twice(int value) {
    return value * NCC_INCLUDE_STEP;
}

int main(void) {
    printf("%s %d %d\n",
           NCC_INCLUDE_TEXT,
           ncc_include_twice(NCC_INCLUDE_BASE),
           EOF);
    return 0;
}
