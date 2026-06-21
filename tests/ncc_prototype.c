#include <nlibc.h>

int add3(int a, int b, int c);
int twice(int value);
char pick(int index);

int main() {
    int value;
    char ch;

    value = twice(add3(2, 3, 4));
    ch = pick(value % 3);
    printf("proto %d %c\n", value, ch);
    return 0;
}

char pick(int index) {
    char letters[4];

    letters[0] = 'A';
    letters[1] = 'B';
    letters[2] = 'C';
    letters[3] = 0;
    return letters[index];
}

int twice(int value) {
    return value * 2;
}

int add3(int a, int b, int c) {
    return a + b + c;
}
