#include <nlibc.h>

struct point {
    int x;
    int y;
};

int global_values[4] = {1, 2, 3};
struct point global_point = {10, 20};
char global_text[] = "global";

int main(void) {
    int values[4] = {4, 5};
    struct point point = {30, 40};
    char text[] = "local";

    printf("init %d %d %d %d %s\n",
           global_values[2] + global_values[3],
           global_point.x + global_point.y,
           values[1] + values[3],
           point.x + point.y,
           text);
    printf("%s\n", global_text);
    return 0;
}
