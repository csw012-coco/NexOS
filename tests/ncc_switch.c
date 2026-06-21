#include <nlibc.h>

enum command {
    COMMAND_START = 1,
    COMMAND_STOP,
    COMMAND_PAUSE
};

int classify(int command) {
    int result;

    result = 0;
    switch (command) {
    case COMMAND_START:
        result = 10;
        break;
    case COMMAND_STOP:
    case COMMAND_PAUSE:
        result = 20;
        break;
    default:
        result = 30;
    }
    return result;
}

int main(void) {
    int i;
    int sum;

    sum = 0;
    for (i = 0; i < 5; i++) {
        switch (i) {
        case 1:
            sum += 1;
            continue;
        case 2:
            sum += 2;
            break;
        default:
            sum += 4;
        }
        sum += 10;
    }

    printf("switch %d %d %d %d %d\n",
           classify(COMMAND_START),
           classify(COMMAND_STOP),
           classify(COMMAND_PAUSE),
           classify(99),
           sum);
    return 0;
}
