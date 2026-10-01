#pragma once
#include "Chart.h"
#include "Color.h"
#include <array>
#include <cstdint>
#include <string>
namespace cm {
struct Observation {
  RGB rgb{};
  double dispersion = 0;
  int valid = 0, candidate = 0;
  uint32_t flags = 0;
};
struct Capture {
  std::string name;
  int chartModel = 0;
  uint64_t revision = 0;
  double time = 0;
  Geometry geometry{};
  std::array<Observation, 32> patch{};
  int width = 0, height = 0;
  std::array<int, 4> bounds{};
  double scaleX = 1, scaleY = 1, par = 1;
};
enum class MatchMethod { Harmonic = 0, RadialLegacy = 1, Rbf = 2 };
struct RadialAnchor {
  double x = 0, y = 0, hue = 0, saturation = 0, weight = 0;
};
constexpr int radialGridSize = 64;
struct RadialLut {
  std::array<float, radialGridSize * radialGridSize * 2> values{};
};
enum class RbfSpace { Linear = 0, Intermediate = 1 };
struct Solution {
  bool valid = false;
  double stops = 0;
  RGB neutralLog{};
  std::array<double, 3> hue{}, sat{};
  MatchMethod method = MatchMethod::Harmonic;
  std::array<RadialAnchor, 32> radial{};
  int radialCount = 0;
  std::array<RGB, 32> rbfCenters{}, rbfWeights{};
  std::array<RGB, 4> rbfAffine{};
  int rbfCount = 0;
  double rbfSupport = 0;
  RbfSpace rbfSpace = RbfSpace::Linear; // CM5 and earlier used linear DWG.
  int neutralCount = 0, colorCount = 0;
  double exposureMAD = 0;
};
struct Amounts {
  double hue = 1, sat = 1, exposure = 1, neutral = 1;
  bool bypass = false;
  double biasWeight = 1;
};
struct SolveResult {
  Solution solution{};
  std::string error;
};
SolveResult solve(const Capture &hero, const Capture &target,
                  const Geometry &geometry,
                  MatchMethod method = MatchMethod::Harmonic);
RadialLut makeRadialLut(const Solution &solution);
RGB transform(RGB encoded, const Solution &solution, const Amounts &amounts,
              const RadialLut *lut = nullptr);
} // namespace cm
