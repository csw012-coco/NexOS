#include <nlibc.h>

int main() {
    char text[7];
    int values[3];
    int *p;
    char *cp;
    int total;

    p = values;
    cp = text;

    total = sizeof(char);
    total += sizeof(int);
    total += sizeof(int *);
    total += sizeof(text);
    total += sizeof(values);
    total += sizeof(p);
    total += sizeof(*p);
    total += sizeof cp;
    total += sizeof *cp;

    printf("sizeof %d\n", total);
    return 0;
}
