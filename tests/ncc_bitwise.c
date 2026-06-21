#include <nlibc.h>

int main(void) {
    int value;
    int mask;
    int precedence;
    int shifted;
    int negative;

    value = 0x12;
    value |= 1 << 5;
    value ^= 3;
    value &= 0x3f;
    value <<= 2;
    value >>= 1;

    mask = ~0 & 0xff;
    precedence = 1 | 2 & 4;
    shifted = 3 << 2 + 1;
    negative = -8 >> 1;

    printf("bits %d %d %d %d %d\n",
           value,
           mask,
           precedence,
           shifted,
           negative);
    return 0;
}
