#include <math.h>

enum {
    MATH_TERMS = 12
};

static const double math_pi = 3.14159265358979323846;
static const double math_quarter_pi = 0.78539816339744830962;
static const double math_half_pi = 1.57079632679489661923;
static const double math_two_pi = 6.28318530717958647692;

double fabs(double value) {
    return value < 0.0 ? -value : value;
}

static double math_reduce_radians(double value) {
    while (value > math_pi) {
        value -= math_two_pi;
    }
    while (value < -math_pi) {
        value += math_two_pi;
    }
    return value;
}

double sin(double value) {
    double term;
    double sum;
    double x2;

    value = math_reduce_radians(value);
    if (value > math_half_pi) {
        value = math_pi - value;
    } else if (value < -math_half_pi) {
        value = -math_pi - value;
    }

    term = value;
    sum = value;
    x2 = value * value;
    for (int i = 1; i < MATH_TERMS; i++) {
        double a = (double)(2 * i);
        double b = (double)(2 * i + 1);

        term = -term * x2 / (a * b);
        sum += term;
    }
    return sum;
}

static double math_cos(double value) {
    double term;
    double sum;
    double x2;

    value = math_reduce_radians(value);
    if (value > math_half_pi) {
        value = math_pi - value;
        term = -1.0;
    } else if (value < -math_half_pi) {
        value = -math_pi - value;
        term = -1.0;
    } else {
        term = 1.0;
    }

    sum = term;
    x2 = value * value;
    for (int i = 1; i < MATH_TERMS; i++) {
        double a = (double)(2 * i - 1);
        double b = (double)(2 * i);

        term = -term * x2 / (a * b);
        sum += term;
    }
    return sum;
}

double tan(double value) {
    double c = math_cos(value);

    if (fabs(c) < 0.000000000001) {
        return sin(value) < 0.0 ? -1.0e300 : 1.0e300;
    }
    return sin(value) / c;
}

static double math_atan_unit(double value) {
    double term = value;
    double sum = value;
    double x2 = value * value;

    for (int i = 1; i < MATH_TERMS * 2; i++) {
        term = -term * x2;
        sum += term / (double)(2 * i + 1);
    }
    return sum;
}

double atan(double value) {
    if (value > 1.0) {
        return math_half_pi - math_atan_unit(1.0 / value);
    }
    if (value < -1.0) {
        return -math_half_pi - math_atan_unit(1.0 / value);
    }
    if (value > 0.41421356237309504880) {
        return math_quarter_pi +
               math_atan_unit((value - 1.0) / (value + 1.0));
    }
    if (value < -0.41421356237309504880) {
        return -math_quarter_pi +
               math_atan_unit((value + 1.0) / (1.0 - value));
    }
    return math_atan_unit(value);
}
