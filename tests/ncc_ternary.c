#include <nlibc.h>

int touch(int *value, int amount) {
    *value += amount;
    return amount;
}

int main(void) {
    int hit;
    int a;
    int b;
    int nested;
    int selected;
    char *name;

    hit = 0;
    a = 1 ? touch(&hit, 10) : touch(&hit, 100);
    b = 0 ? touch(&hit, 100) : touch(&hit, 20);
    nested = 0 ? 1 : 1 ? 30 : 40;
    name = a > b ? "a" : "b";
    selected = 1 ? (a = 7) : (b = 8);

    printf("ternary %d %d %d %s %d\n",
           b,
           nested,
           hit,
           name,
           a + selected);
    return 0;
}
