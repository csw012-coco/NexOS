#include <nlibc.h>

int counter = 5;
int zero;
char marker = 'G';
char buffer[4];
int numbers[3];
char *cursor;

int bump(void);

int main() {
    int value;

    buffer[0] = marker;
    buffer[1] = 'L';
    buffer[2] = 0;
    numbers[0] = 7;
    numbers[1] = 8;
    numbers[2] = 9;
    cursor = buffer;

    value = bump();
    value += zero;
    value += numbers[1];
    value += cursor[1];

    printf("global %d %s\n", value, buffer);
    return 0;
}

int bump(void) {
    counter += 2;
    zero++;
    return counter;
}
