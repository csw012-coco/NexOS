#include <nlibc.h>

int side_effect(int *value) {
    *value += 100;
    return *value;
}

int main() {
    int i;
    int sum;
    int hit;
    int zero;

    sum = 0;
    hit = 0;
    zero = 0;

    for (i = 0; i < 10; i++) {
        if ((i % 3) == 1 || i == 8) {
            sum += i;
        }
    }

    if (0 && side_effect(&hit)) {
        sum += 1000;
    }
    if (1 || side_effect(&hit)) {
        sum += 1;
    }
    if ((sum > 0 && hit == 0) && (1 || (10 % zero))) {
        sum += 2;
    }

    printf("logic %d %d\n", sum, hit);
    return 0;
}
