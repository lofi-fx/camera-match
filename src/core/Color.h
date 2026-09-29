#pragma once
#include <array>
namespace cm {
struct RGB {
  double r = 0, g = 0, b = 0;
};
struct Lab {
  double L = 0, a = 0, b = 0;
};
RGB operator+(RGB a, RGB b);
RGB operator-(RGB a, RGB b);
RGB operator*(RGB a, double s);
RGB operator/(RGB a, double s);
bool finite(RGB a);
double luminance(RGB a);
double decodeIntermediate(double x);
double encodeIntermediate(double x);
RGB decode(RGB a);
RGB encode(RGB a);
Lab toOklab(RGB linearDWG);
RGB fromOklab(Lab lab);
} // namespace cm
