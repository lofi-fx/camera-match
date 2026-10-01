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
enum class RbfSpace { Linear = 0, Intermediate = 1 };
struct Solution {
  bool valid = false;
  std::array<RGB, 32> rbfCenters{}, rbfWeights{};
  std::array<RGB, 4> rbfAffine{};
  int rbfCount = 0;
  double rbfSupport = 0;
  RbfSpace rbfSpace = RbfSpace::Linear; // CM5 and earlier used linear DWG.
  int neutralCount = 0, colorCount = 0;
};
struct Amounts {
  bool bypass = false;
  double biasWeight = 1;
  double rbfSat = 1, rbfExposure = 1;
};
struct SolveResult {
  Solution solution{};
  std::string error;
};
SolveResult solve(const Capture &hero, const Capture &target,
                  const Geometry &geometry);
RGB transform(RGB encoded, const Solution &solution, const Amounts &amounts);
} // namespace cm
