#include <stdio.h>

int sum3(int *values) {
    return values[0] + *(values + 1) + values[2];
}

char echo_char(char value) {
    return value;
}

int main(void) {
    char text[4];
    char *cursor;
    int values[3];
    int *numbers;

    text[0] = 'n';
    text[1] = 'c';
    text[2] = 'c';
    text[3] = 0;
    cursor = text;
    cursor[0] = 'N';
    cursor[1] = 'C';
    cursor[2] = 'C';

    values[0] = 10;
    values[1] = 20;
    values[2] = 30;
    numbers = &values[0];

    printf("%s %d %c\n", text, sum3(numbers), echo_char(*cursor));
    return 0;
}
