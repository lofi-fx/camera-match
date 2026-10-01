#include "Match.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace cm {
static double clamp(double x, double a, double b) {
  return std::max(a, std::min(b, x));
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
  s.rbfSpace = RbfSpace::Intermediate;
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
  // Color Workspace's DWG/DI preset fits encoded RGB, not linear light.
  // Captures remain scene-linear; encode them into the fitting space here.
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
  // Sparse or mismatched patches can fit a smooth function that folds in
  // shadows. Check actual scene-linear brightness along an encoded neutral
  // ramp before accepting it. This is a fit-time check, never a render clamp.
  double upper = 1.;
  for (int i = 0; i < n; ++i)
    upper = std::max({upper, s.rbfCenters[i].r, s.rbfCenters[i].g,
                     s.rbfCenters[i].b});
  double previous = luminance(decode(evaluateRbf(s, {})));
  for (int i = 1; i <= 4096; ++i) {
    double v = upper * i / 4096.;
    RGB mapped = decode(evaluateRbf(s, {v, v, v}));
    double y = luminance(mapped);
    if (!finite(mapped) || !std::isfinite(y) || y < previous - 1e-9) {
      result.error = "RBF rejected: shadow/highlight brightness reverses. "
                     "Check Rotate chart and sample alignment on both clips, "
                     "then recapture the reference and target. Previous correction kept.";
      return result;
    }
    previous = y;
  }
  s.valid = true;
  return result;
}
SolveResult solve(const Capture &hero, const Capture &target, const Geometry &geo) {
  if (hero.chartModel != target.chartModel || geo.model != hero.chartModel)
    return {{}, "Reference and target chart models differ"};
  return solveRbf(hero, target, geo);
}
// Attenuate components of the weighted RBF result, without another fit.
static RGB rbfAmounts(RGB input, RGB matched, double saturation, double exposure) {
  saturation = clamp(saturation, 0, 1);
  exposure = clamp(exposure, 0, 1);
  if (saturation == 1 && exposure == 1)
    return matched;
  RGB source = decode(input), target = decode(matched);
  double sourceY = luminance(source), targetY = luminance(target);
  // Relative saturation and exposure ratios have no meaningful definition
  // for nonpositive luminance. Preserve the signed RBF result there.
  if (sourceY <= 1e-7 || targetY <= 1e-7)
    return matched;
  double desiredY = exposure == 1 ? targetY : exposure == 0 ? sourceY
      : sourceY * std::exp(std::log(targetY / sourceY) * exposure);
  RGB result = target;
  if (saturation < 1) {
    Lab src = toOklab(source), dst = toOklab(target);
    double c = std::hypot(dst.a, dst.b);
    if (src.L > 1e-7 && dst.L > 1e-7 && c > 0) {
      double relative = c / dst.L;
      // Fade the direction-dependent operation continuously at neutral,
      // where hue is undefined, rather than dividing by tiny chroma.
      double t = clamp(relative / 1e-4, 0, 1);
      double fade = t * t * (3 - 2 * t);
      double factor = 1 + (1 - saturation) * fade *
          (std::hypot(src.a, src.b) / src.L / relative - 1);
      RGB adjusted = fromOklab({dst.L, dst.a * factor, dst.b * factor});
      if (finite(adjusted) && luminance(adjusted) > 1e-7)
        result = adjusted;
    }
  }
  RGB out = encode(result * (desiredY / luminance(result)));
  return finite(out) ? out : matched;
}
RGB transform(RGB input, const Solution &s, const Amounts &a) {
  if (a.bypass || !s.valid || !finite(input) || a.biasWeight <= 0)
    return input;
  const bool di = s.rbfSpace == RbfSpace::Intermediate;
  RGB x = di ? input : decode(input);
  RGB matched = evaluateRbf(s, x);
  RGB out = x + (matched - x) * clamp(a.biasWeight, 0, 2);
  if (!di)
    out = encode(out);
  return finite(out) ? rbfAmounts(input, out, a.rbfSat, a.rbfExposure) : input;
}
} // namespace cm
