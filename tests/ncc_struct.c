#include <nlibc.h>

struct Pair {
    int a;
    char tag;
    int values[2];
};

struct Pair global_pair;
struct Pair pair_list[2];

int sum_pair(struct Pair *pair);

int main() {
    struct Pair local;
    struct Pair *ptr;
    int total;

    local.a = 10;
    local.tag = 'S';
    local.values[0] = 2;
    local.values[1] = 3;
    ptr = &local;

    global_pair.a = ptr->a + local.values[1];
    global_pair.tag = local.tag;
    global_pair.values[0] = 4;
    global_pair.values[1] = 5;

    pair_list[0].a = 1;
    pair_list[0].tag = 'A';
    pair_list[0].values[0] = 6;
    pair_list[0].values[1] = 7;

    pair_list[1].a = 8;
    pair_list[1].tag = 'B';
    pair_list[1].values[0] = 9;
    pair_list[1].values[1] = 10;

    total = sum_pair(&global_pair);
    total += sum_pair(&pair_list[0]);
    total += pair_list[1].values[1];
    total += sizeof(struct Pair);

    printf("struct %d %c %d\n", total, ptr->tag, sizeof(pair_list));
    return 0;
}

int sum_pair(struct Pair *pair) {
    return pair->a + pair->values[0] + pair->values[1];
}
