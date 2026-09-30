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
static double distance(RGB a, RGB b) {
  return std::sqrt((a.r - b.r) * (a.r - b.r) +
                   (a.g - b.g) * (a.g - b.g) +
                   (a.b - b.b) * (a.b - b.b));
}
static RGB evaluateRbf(const Solution &s, RGB x) {
  RGB out = s.rbfAffine[0] + s.rbfAffine[1] * x.r +
            s.rbfAffine[2] * x.g + s.rbfAffine[3] * x.b;
  for (int j = 0; j < s.rbfCount; ++j) {
    double r = distance(x, s.rbfCenters[j]) / s.rbfSupport;
    out = out + s.rbfWeights[j] * std::exp(-r * r);
  }
  return finite(out) ? out : x;
}
static SolveResult solveRbf(const Capture &hero, const Capture &target,
                            const Geometry &geo) {
  SolveResult result;
  auto &s = result.solution;
  s.method = MatchMethod::Rbf;
  const auto &patches = layout(geo.model);
  for (size_t j = 0; j < patches.size(); ++j) {
    if (!geo.included[j] || hero.patch[j].valid < 16 ||
        target.patch[j].valid < 16)
      continue;
    RGB a = encode(target.patch[j].rgb), b = encode(hero.patch[j].rgb);
    if (!finite(a) || !finite(b))
      continue;
    int index = s.rbfCount++;
    s.rbfCenters[index] = a;
    s.rbfWeights[index] = b; // Desired outputs until the linear solve finishes.
    if (patches[j].role == Role::Neutral)
      ++s.neutralCount;
    else
      ++s.colorCount;
  }
  if (s.rbfCount < 7) {
    result.error = "RBF needs at least seven usable chart patches";
    return result;
  }
  // Match Color Workspace's DWG/DI preset: black anchor, narrow support,
  // and stronger regularization to avoid local color oscillation.
  s.rbfCenters[s.rbfCount] = {};
  s.rbfWeights[s.rbfCount] = {};
  ++s.rbfCount;
  int n = s.rbfCount;
  s.rbfSupport = .1;
  int size = n + 4;
  std::vector<std::vector<double>> a(size, std::vector<double>(size + 3));
  for (int i = 0; i < n; ++i) {
    RGB x = s.rbfCenters[i], y = s.rbfWeights[i];
    for (int j = 0; j < n; ++j) {
      double r = distance(x, s.rbfCenters[j]) / s.rbfSupport;
      a[i][j] = std::exp(-r * r);
    }
    a[i][i] += .2;
    double p[4] = {1, x.r, x.g, x.b};
    for (int j = 0; j < 4; ++j)
      a[i][n + j] = a[n + j][i] = p[j];
    a[i][size] = y.r;
    a[i][size + 1] = y.g;
    a[i][size + 2] = y.b;
  }
  for (int j = 0; j < 4; ++j)
    a[n + j][n + j] = -1e-7;
  for (int col = 0; col < size; ++col) {
    int pivot = col;
    for (int row = col + 1; row < size; ++row)
      if (std::abs(a[row][col]) > std::abs(a[pivot][col]))
        pivot = row;
    if (std::abs(a[pivot][col]) < 1e-12) {
      result.error = "RBF fit is unstable; select more distinct patches";
      return result;
    }
    std::swap(a[col], a[pivot]);
    double divisor = a[col][col];
    for (int k = col; k < size + 3; ++k)
      a[col][k] /= divisor;
    for (int row = 0; row < size; ++row) {
      if (row == col)
        continue;
      double factor = a[row][col];
      for (int k = col; k < size + 3; ++k)
        a[row][k] -= factor * a[col][k];
    }
  }
  for (int i = 0; i < n; ++i)
    s.rbfWeights[i] = {a[i][size], a[i][size + 1], a[i][size + 2]};
  for (int i = 0; i < 4; ++i)
    s.rbfAffine[i] = {a[n + i][size], a[n + i][size + 1],
                      a[n + i][size + 2]};
  s.valid = true;
  return result;
}
SolveResult solve(const Capture &hero, const Capture &target,
                  const Geometry &geo, MatchMethod method) {
  SolveResult result;
  if (hero.chartModel != target.chartModel || geo.model != hero.chartModel) {
    result.error = "Hero and target chart models differ";
    return result;
  }
  if (method == MatchMethod::Rbf)
    return solveRbf(hero, target, geo);
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
  s.method = method;
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
    if (method == MatchMethod::RadialLegacy && s.radialCount < int(s.radial.size()))
      s.radial[s.radialCount++] = {lb.a / lb.L, lb.b / lb.L,
                                   clamp(dh, -M_PI / 6, M_PI / 6),
                                   clamp(ds, -std::log(2.), std::log(2.)), w};
    hr.push_back(
        {1, std::sin(h), std::cos(h), clamp(dh, -M_PI / 6, M_PI / 6), w});
    sr.push_back({1, std::sin(h), std::cos(h),
                  clamp(ds, -std::log(2.), std::log(2.)), w});
  }
  s.colorCount = int(hr.size());
  int sectorCount = 0;
  for (bool v : sectors)
    sectorCount += v;
  if (method == MatchMethod::RadialLegacy && hr.size() >= 4 && sectorCount >= 4) {
    s.valid = true;
  } else if (method == MatchMethod::Harmonic && hr.size() >= 4 &&
             sectorCount >= 4 && regress(hr, s.hue) &&
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
  } else {
    s.radialCount = 0;
    result.error = "Neutral/exposure match only: need chromatic patches "
                   "spanning four hue regions";
  }
  if (!s.valid)
    s.valid = true;
  return result;
}
static std::array<double, 2> radialAt(const Solution &s, double x, double y) {
  double hue = 0, sat = 0, total = .35;
  for (int j = 0; j < s.radialCount; ++j) {
    const auto &a = s.radial[j];
    double distance = std::hypot(x - a.x, y - a.y) / .38;
    if (distance >= 1)
      continue;
    double t = 1 - distance;
    double w = a.weight * t * t * t * t * (1 + 4 * distance);
    hue += w * a.hue;
    sat += w * a.saturation;
    total += w;
  }
  return {hue / total, sat / total};
}
RadialLut makeRadialLut(const Solution &s) {
  RadialLut lut;
  for (int y = 0; y < radialGridSize; ++y)
    for (int x = 0; x < radialGridSize; ++x) {
      auto v = radialAt(s, -1. + 2. * x / (radialGridSize - 1),
                        -1. + 2. * y / (radialGridSize - 1));
      int i = (y * radialGridSize + x) * 2;
      lut.values[i] = float(v[0]);
      lut.values[i + 1] = float(v[1]);
    }
  return lut;
}
constexpr double rbfMin = 0, rbfMax = 1;
RbfLut makeRbfLut(const Solution &s) {
  RbfLut lut;
  for (int r = 0; r < rbfGridSize; ++r)
    for (int g = 0; g < rbfGridSize; ++g)
      for (int b = 0; b < rbfGridSize; ++b) {
        RGB x{rbfMin + (rbfMax - rbfMin) * r / (rbfGridSize - 1),
              rbfMin + (rbfMax - rbfMin) * g / (rbfGridSize - 1),
              rbfMin + (rbfMax - rbfMin) * b / (rbfGridSize - 1)};
        RGB y = evaluateRbf(s, x);
        int i = ((r * rbfGridSize + g) * rbfGridSize + b) * 3;
        lut.values[i] = float(y.r);
        lut.values[i + 1] = float(y.g);
        lut.values[i + 2] = float(y.b);
      }
  return lut;
}
static RGB sampleRbf(const RbfLut &lut, RGB x) {
  auto coordinate = [](double v) {
    return (v - rbfMin) * (rbfGridSize - 1) / (rbfMax - rbfMin);
  };
  double p[3] = {coordinate(x.r), coordinate(x.g), coordinate(x.b)};
  for (double v : p)
    if (v < 0 || v > rbfGridSize - 1)
      return x;
  int lo[3] = {int(p[0]), int(p[1]), int(p[2])};
  int hi[3] = {std::min(lo[0] + 1, rbfGridSize - 1),
               std::min(lo[1] + 1, rbfGridSize - 1),
               std::min(lo[2] + 1, rbfGridSize - 1)};
  double t[3] = {p[0] - lo[0], p[1] - lo[1], p[2] - lo[2]};
  RGB y{};
  for (int mask = 0; mask < 8; ++mask) {
    int r = mask & 1 ? hi[0] : lo[0];
    int g = mask & 2 ? hi[1] : lo[1];
    int b = mask & 4 ? hi[2] : lo[2];
    double w = (mask & 1 ? t[0] : 1 - t[0]) *
               (mask & 2 ? t[1] : 1 - t[1]) *
               (mask & 4 ? t[2] : 1 - t[2]);
    int i = ((r * rbfGridSize + g) * rbfGridSize + b) * 3;
    y = y + RGB{lut.values[i], lut.values[i + 1], lut.values[i + 2]} * w;
  }
  return y;
}
static std::array<double, 2> sampleRadial(const RadialLut &lut, double x,
                                          double y) {
  double gx = clamp((x + 1) * .5 * (radialGridSize - 1), 0,
                    radialGridSize - 1),
         gy = clamp((y + 1) * .5 * (radialGridSize - 1), 0,
                    radialGridSize - 1);
  int ix = int(gx), iy = int(gy);
  int jx = std::min(ix + 1, radialGridSize - 1),
      jy = std::min(iy + 1, radialGridSize - 1);
  double tx = gx - ix, ty = gy - iy;
  std::array<double, 2> result{};
  for (int c = 0; c < 2; ++c) {
    auto at = [&](int px, int py) {
      return double(lut.values[(py * radialGridSize + px) * 2 + c]);
    };
    result[c] = (1 - ty) * ((1 - tx) * at(ix, iy) + tx * at(jx, iy)) +
                ty * ((1 - tx) * at(ix, jy) + tx * at(jx, jy));
  }
  return result;
}
RGB transform(RGB input, const Solution &s, const Amounts &a,
              const RadialLut *lut, const RbfLut *rbfLut) {
  if (s.method == MatchMethod::Rbf) {
    if (a.bypass || !s.valid || !finite(input) || a.biasWeight <= 0)
      return input;
    RGB matched = rbfLut ? sampleRbf(*rbfLut, input) : evaluateRbf(s, input);
    RGB out = input + (matched - input) * clamp(a.biasWeight, 0, 2);
    return finite(out) ? out : input;
  }
  if (a.bypass || !s.valid ||
      (((a.hue == 0 && a.sat == 0) ||
        (s.method == MatchMethod::RadialLegacy && a.biasWeight == 0)) &&
       a.exposure == 0 && a.neutral == 0) ||
      !finite(input))
    return input;
  RGB x = decode(input);
  double y = luminance(x);
  if (a.neutral > 0 && y > 1e-7)
    x = neutral(x, s.neutralLog, clamp(a.neutral, 0, 1));
  if ((a.hue > 0 || a.sat > 0) &&
      (s.method != MatchMethod::RadialLegacy || a.biasWeight > 0) && y > 1e-6) {
    Lab l = toOklab(x);
    double c = std::hypot(l.a, l.b);
    if (l.L > 1e-5 && c / l.L > .005) {
      double h = std::atan2(l.b, l.a),
             dh = s.hue[0] + s.hue[1] * std::sin(h) + s.hue[2] * std::cos(h),
             ds = s.sat[0] + s.sat[1] * std::sin(h) + s.sat[2] * std::cos(h);
      if (s.method == MatchMethod::RadialLegacy) {
        auto v = lut ? sampleRadial(*lut, l.a / l.L, l.b / l.L)
                     : radialAt(s, l.a / l.L, l.b / l.L);
        dh = v[0] * clamp(a.biasWeight, 0, 2);
        ds = v[1] * clamp(a.biasWeight, 0, 2);
      }
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
