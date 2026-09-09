#include "user/libc/include/stdio.h"
#include "user/libc/include/nexos/system.h"
#include "user/libc/include/stdlib.h"

int calc(int a, int b, char op, int* result) {
    switch (op) {
        case '+':
            *result = a + b;
            return 0;
        case '-':
            *result = a - b;
            return 0;
        case '*':
            *result = a * b;
            return 0;
        case '/':
            if (b != 0) {
                *result = a / b;
                return 0;
            } else {
                return 1; // Return 1 to indicate an error
            }
        default:
            return 2; // Return 2 to indicate an error
    }
}

int main(int argc, char* argv[]) {
    int a = atoi(argv[1]);
    int b = atoi(argv[3]);
    char op = argv[2][0];
    int result;
    
    if (argc != 4) {
        char input[128];
        printf("Enter calculation (e.g., 3 + 4): ");
        fgets(input, sizeof(input), stdin);
        sscanf(input, "%d %c %d", &a, &op, &b);
    }

    int error_code = calc(a, b, op, &result);
    if (error_code == 0) {
        printf("Result: %d\n", result);
    } else if (error_code == 1) {
        printf("Error: Division by zero\n");
        printf("Calculation failed.\n");
    } else if (error_code == 2) {
        printf("Error: Invalid operator '%c'\n", op);
        printf("Calculation failed.\n");
    } else {
        printf("Calculation failed.\n");
    }
    return 0;
}