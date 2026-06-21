#include <nlibc.h>

#define COUNT 4
#define START 3
#define LETTER 'D'
#define LABEL "define"
#define EXPR (COUNT * START + 2)

int values[COUNT];

int main() {
    int i;
    int total;
    char ch;

    total = EXPR;
    ch = LETTER;
    for (i = 0; i < COUNT; i++) {
        values[i] = START + i;
        total += values[i];
    }
    printf("%s %d %c %d\n", LABEL, total, ch, sizeof(values));
    return 0;
}
