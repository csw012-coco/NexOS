#include <stdio.h>

int main(void) {
    int i;
    int j;
    int k;
    int total;

    total = 0;
    for (i = 0; i < 7; i = i + 1) {
        if (i == 2) {
            continue;
        }
        if (i == 5) {
            break;
        }
        total = total + i;
    }

    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 4; j = j + 1) {
            if (i == 2) {
                break;
            }
            if (j == 1) {
                continue;
            }
            total = total + 1;
        }
    }

    k = 0;
    while (k < 5) {
        k = k + 1;
        if (k == 2) {
            continue;
        }
        if (k == 4) {
            break;
        }
        total = total + k;
    }

    printf("loops %d\n", total);
    return 0;
}
