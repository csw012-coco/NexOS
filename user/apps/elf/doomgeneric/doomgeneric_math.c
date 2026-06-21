#include <math.h>
#include <stdlib.h>

double sin(double value) {
    double result;

    __asm__ volatile("fldl %1; fsin; fstpl %0" : "=m"(result) : "m"(value));
    return result;
}

double tan(double value) {
    double result;

    __asm__ volatile("fldl %1; fptan; fstp %%st(0); fstpl %0"
                     : "=m"(result)
                     : "m"(value));
    return result;
}

double atan(double value) {
    double result;

    __asm__ volatile("fldl %1; fld1; fpatan; fstpl %0" : "=m"(result) : "m"(value));
    return result;
}

double fabs(double value) {
    return value < 0.0 ? -value : value;
}

double atof(const char *text) {
    double value = 0.0;
    double scale = 1.0;
    int sign = 1;

    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }
    if (*text == '-') {
        sign = -1;
        text++;
    } else if (*text == '+') {
        text++;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10.0 + (*text++ - '0');
    }
    if (*text == '.') {
        text++;
        while (*text >= '0' && *text <= '9') {
            scale *= 0.1;
            value += (*text++ - '0') * scale;
        }
    }
    return sign < 0 ? -value : value;
}
