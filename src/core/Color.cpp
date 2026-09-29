#include "Color.h"
#include <cmath>
namespace cm {
RGB operator+(RGB a, RGB b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
RGB operator-(RGB a, RGB b) { return {a.r - b.r, a.g - b.g, a.b - b.b}; }
RGB operator*(RGB a, double s) { return {a.r * s, a.g * s, a.b * s}; }
RGB operator/(RGB a, double s) { return a * (1 / s); }
bool finite(RGB a) {
  return std::isfinite(a.r) && std::isfinite(a.g) && std::isfinite(a.b);
}
double luminance(RGB a) {
  return .27411851 * a.r + .87363190 * a.g - .1477249265 * a.b;
}
double decodeIntermediate(double x) {
  return x <= .02740668 ? x / 10.44426855
                        : std::exp2(x / .07329248 - 7) - .0075;
}
double encodeIntermediate(double x) {
  return x <= .00262409 ? x * 10.44426855
                        : (std::log2(x + .0075) + 7) * .07329248;
}
RGB decode(RGB a) {
  return {decodeIntermediate(a.r), decodeIntermediate(a.g),
          decodeIntermediate(a.b)};
}
RGB encode(RGB a) {
  return {encodeIntermediate(a.r), encodeIntermediate(a.g),
          encodeIntermediate(a.b)};
}
static RGB mul(const double m[9], RGB a) {
  return {m[0] * a.r + m[1] * a.g + m[2] * a.b,
          m[3] * a.r + m[4] * a.g + m[5] * a.b,
          m[6] * a.r + m[7] * a.g + m[8] * a.b};
}
static const double dwgXYZ[9] = {.70062239,  .14877482,  .10105872,
                                 .27411851,  .87363190,  -.14775041,
                                 -.09896291, -.13789533, 1.32591599};
static const double xyzDWG[9] = {1.51667204, -.28147805, -.14696363,
                                 -.46491710, 1.25142378, .17488461,
                                 .06484905,  .10913934,  .76141462};
static const double xyzLms[9] = {.8189330101, .3618667424, -.1288597137,
                                 .0329845436, .9293118715, .0361456387,
                                 .0482003018, .2643662691, .6338517070};
static const double lmsXYZ[9] = {
    1.227013851103521, -.557799980651822, .281256148966468,
    -.040580178423281, 1.112256869616830, -.071676678665601,
    -.076381284505707, -.421481978418013, 1.586163220440795};
static const double lmsLab[9] = {.2104542553,  .7936177850,   -.0040720468,
                                 1.9779984951, -2.4285922050, .4505937099,
                                 .0259040371,  .7827717662,   -.8086757660};
static const double labLms[9] = {1, .396337777376175,  .215803757309914,
                                 1, -.105561345815659, -.063854172825813,
                                 1, -.089484177529812, -1.291485548019410};
Lab toOklab(RGB dwg) {
  RGB l = mul(xyzLms, mul(dwgXYZ, dwg));
  l = {std::cbrt(l.r), std::cbrt(l.g), std::cbrt(l.b)};
  RGB v = mul(lmsLab, l);
  return {v.r, v.g, v.b};
}
RGB fromOklab(Lab lab) {
  RGB l = mul(labLms, {lab.L, lab.a, lab.b});
  l = {l.r * l.r * l.r, l.g * l.g * l.g, l.b * l.b * l.b};
  return mul(xyzDWG, mul(lmsXYZ, l));
}
} // namespace cm
