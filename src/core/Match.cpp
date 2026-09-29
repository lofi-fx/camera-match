#include "Match.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace cm {
static double clamp(double x, double a, double b) {
  return std::max(a, std::min(b, x));
}
static double median(std::vector<double> v) {
  if (v.empty())
    return 0;
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * .5;
}
static RGB neutral(RGB x, RGB logs, double amount) {
  double y = luminance(x);
  if (y <= 1e-7)
    return x;
  RGB z{x.r * std::exp(logs.r * amount), x.g * std::exp(logs.g * amount),
        x.b * std::exp(logs.b * amount)};
  double zy = luminance(z);
  return zy > 1e-7 && finite(z) ? z * (y / zy) : x;
}
static bool regress(const std::vector<std::array<double, 5>> &rows,
                    std::array<double, 3> &out) {
  double a[3][4]{};
  for (int i = 0; i < 3; i++)
    a[i][i] = .03;
  for (auto r : rows)
    for (int i = 0; i < 3; i++) {
      a[i][3] += r[4] * r[i] * r[3];
      for (int j = 0; j < 3; j++)
        a[i][j] += r[4] * r[i] * r[j];
    }
  for (int k = 0; k < 3; k++) {
    int best = k;
    for (int i = k + 1; i < 3; i++)
      if (std::abs(a[i][k]) > std::abs(a[best][k]))
        best = i;
    if (std::abs(a[best][k]) < 1e-6)
      return false;
    if (best != k)
      for (int j = k; j < 4; j++)
        std::swap(a[best][j], a[k][j]);
    double d = a[k][k];
    for (int j = k; j < 4; j++)
      a[k][j] /= d;
    for (int i = 0; i < 3; i++)
      if (i != k) {
        double t = a[i][k];
        for (int j = k; j < 4; j++)
          a[i][j] -= t * a[k][j];
      }
  }
  for (int i = 0; i < 3; i++)
    out[i] = a[i][3];
  return true;
}
SolveResult solve(const Capture &hero, const Capture &target,
                  const Geometry &geo) {
  SolveResult result;
  if (hero.chartModel != target.chartModel || geo.model != hero.chartModel) {
    result.error = "Hero and target chart models differ";
    return result;
  }
  auto &p = layout(hero.chartModel);
  std::vector<double> stops, rr, gg, bb;
  for (size_t i = 0; i < p.size(); i++) {
    if (!geo.included[i] || p[i].role != Role::Neutral)
      continue;
    auto a = hero.patch[i], b = target.patch[i];
    if (a.valid < 16 || b.valid < 16)
      continue;
    double ya = luminance(a.rgb), yb = luminance(b.rgb);
    if (ya < .005 || yb < .005)
      continue;
    stops.push_back(std::log2(ya / yb));
    if (a.rgb.r > 1e-5 && a.rgb.g > 1e-5 && a.rgb.b > 1e-5 && b.rgb.r > 1e-5 &&
        b.rgb.g > 1e-5 && b.rgb.b > 1e-5) {
      rr.push_back(std::log(a.rgb.r / b.rgb.r));
      gg.push_back(std::log(a.rgb.g / b.rgb.g));
      bb.push_back(std::log(a.rgb.b / b.rgb.b));
    }
  }
  if (stops.size() < 3) {
    result.error = "Need at least three usable neutral patches";
    return result;
  }
  auto &s = result.solution;
  s.stops = clamp(median(stops), -4, 4);
  s.neutralCount = int(stops.size());
  std::vector<double> dev;
  for (double x : stops)
    dev.push_back(std::abs(x - s.stops));
  s.exposureMAD = median(dev);
  if (rr.size() >= 3) {
    s.neutralLog = {median(rr), median(gg), median(bb)};
    double common = (s.neutralLog.r + s.neutralLog.g + s.neutralLog.b) / 3;
    s.neutralLog = s.neutralLog - RGB{common, common, common};
    s.neutralLog.r = clamp(s.neutralLog.r, -.7, .7);
    s.neutralLog.g = clamp(s.neutralLog.g, -.7, .7);
    s.neutralLog.b = clamp(s.neutralLog.b, -.7, .7);
  }
  std::vector<std::array<double, 5>> hr, sr;
  bool sectors[6]{};
  for (size_t i = 0; i < p.size(); i++) {
    if (!geo.included[i] ||
        (p[i].role != Role::Chromatic && p[i].role != Role::Skin))
      continue;
    auto a = hero.patch[i], b = target.patch[i];
    if (a.valid < 16 || b.valid < 16)
      continue;
    Lab la = toOklab(a.rgb), lb = toOklab(neutral(b.rgb, s.neutralLog, 1));
    double ca = std::hypot(la.a, la.b), cb = std::hypot(lb.a, lb.b);
    if (la.L < .06 || lb.L < .06 || ca / la.L < .025 || cb / lb.L < .025)
      continue;
    double h = std::atan2(lb.b, lb.a);
    sectors[std::min(5, int((h + M_PI) / (2 * M_PI) * 6))] = true;
    double dh = std::remainder(std::atan2(la.b, la.a) - h, 2 * M_PI),
           ds = std::log((ca / la.L) / (cb / lb.L));
    double w = clamp(std::min(a.valid, b.valid) / 100., .2, 1.);
    if (p[i].role == Role::Skin)
      w *= .6;
    hr.push_back(
        {1, std::sin(h), std::cos(h), clamp(dh, -M_PI / 6, M_PI / 6), w});
    sr.push_back({1, std::sin(h), std::cos(h),
                  clamp(ds, -std::log(2.), std::log(2.)), w});
  }
  s.colorCount = int(hr.size());
  int sectorCount = 0;
  for (bool v : sectors)
    sectorCount += v;
  if (hr.size() >= 4 && sectorCount >= 4 && regress(hr, s.hue) &&
      regress(sr, s.sat)) {
    double maxHue = 0, maxDerivative = 0, maxSat = 0;
    for (int j = 0; j < 360; j++) {
      double h = -M_PI + 2 * M_PI * j / 360.;
      maxHue = std::max(maxHue, std::abs(s.hue[0] + s.hue[1] * std::sin(h) +
                                         s.hue[2] * std::cos(h)));
      maxDerivative = std::max(maxDerivative, std::abs(s.hue[1] * std::cos(h) -
                                                       s.hue[2] * std::sin(h)));
      maxSat = std::max(maxSat, std::abs(s.sat[0] + s.sat[1] * std::sin(h) +
                                         s.sat[2] * std::cos(h)));
    }
    double hs = std::min({1., (M_PI / 6) / std::max(maxHue, 1e-9),
                          .5 / std::max(maxDerivative, 1e-9)}),
           ss = std::min(1., std::log(2.) / std::max(maxSat, 1e-9));
    for (double &v : s.hue)
      v *= hs;
    for (double &v : s.sat)
      v *= ss;
    s.valid = true;
  } else
    result.error = "Neutral/exposure match only: need chromatic patches "
                   "spanning four hue regions";
  if (!s.valid)
    s.valid = true;
  return result;
}
RGB transform(RGB input, const Solution &s, const Amounts &a) {
  if (a.bypass || !s.valid ||
      (a.hue == 0 && a.sat == 0 && a.exposure == 0 && a.neutral == 0) ||
      !finite(input))
    return input;
  RGB x = decode(input);
  double y = luminance(x);
  if (a.neutral > 0 && y > 1e-7)
    x = neutral(x, s.neutralLog, clamp(a.neutral, 0, 1));
  if ((a.hue > 0 || a.sat > 0) && y > 1e-6) {
    Lab l = toOklab(x);
    double c = std::hypot(l.a, l.b);
    if (l.L > 1e-5 && c / l.L > .005) {
      double h = std::atan2(l.b, l.a),
             dh = s.hue[0] + s.hue[1] * std::sin(h) + s.hue[2] * std::cos(h),
             ds = s.sat[0] + s.sat[1] * std::sin(h) + s.sat[2] * std::cos(h);
      double h2 = h + clamp(a.hue, 0, 1) * clamp(dh, -M_PI / 6, M_PI / 6),
             c2 = c * std::exp(clamp(a.sat, 0, 1) *
                               clamp(ds, -std::log(2.), std::log(2.)));
      RGB z = fromOklab({l.L, c2 * std::cos(h2), c2 * std::sin(h2)});
      double zy = luminance(z);
      if (finite(z) && zy > 1e-6) {
        z = z * (y / zy);
        double magnitude =
            std::max({std::abs(z.r), std::abs(z.g), std::abs(z.b)});
        if (magnitude < 100)
          x = z;
      }
    }
  }
  if (a.exposure > 0)
    x = x * std::exp2(clamp(a.exposure, 0, 1) * s.stops);
  RGB out = encode(x);
  return finite(out) ? out : input;
}
} // namespace cm
