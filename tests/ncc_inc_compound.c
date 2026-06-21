#include <nlibc.h>

int main() {
    int values[4];
    int i;
    int sum;
    int old;
    int *p;
    char c;

    values[0] = 3;
    values[1] = 4;
    values[2] = 5;
    values[3] = 99;
    sum = 0;

    for (i = 0; i < 3; i++) {
        sum += values[i];
    }

    p = values;
    old = *p++;
    sum += old;
    sum += *p;
    p += 2;
    p -= 1;
    sum += *p;

    c = 'A';
    c++;
    ++c;
    c -= 1;
    --c;

    printf("inc %d %c\n", sum, c);
    return 0;
}
