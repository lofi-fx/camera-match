#pragma once
#include <array>
#include <string>
#include <vector>
namespace cm {
struct Point {
  double x = 0, y = 0;
};
struct Rect {
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
enum class Role { Chromatic, Skin, Neutral, Illumination, Glossy };
struct Patch {
  const char *id;
  Rect box;
  Role role;
  bool defaultIncluded;
};
const std::vector<Patch> &layout(int model = 0);
struct SampleEdit {
  double dx = 0, dy = 0, w = .5, h = .5;
};
struct Geometry {
  std::array<Point, 4> corners{{{0, 1080}, {1920, 1080}, {1920, 0}, {0, 0}}};
  std::array<SampleEdit, 32> samples{};
  std::array<bool, 32> included{};
  int model = 0;
  int rotation = 0;
  bool mirror = false;
  Geometry();
};
struct Homography {
  double m[9]{};
  double inv[9]{};
  bool valid = false;
  Point project(Point uv) const;
  Point unproject(Point canonical) const;
};
bool validCorners(const std::array<Point, 4> &c);
Homography makeHomography(const Geometry &g);
Point orient(Point uv, int rotation, bool mirror);
Point unorient(Point uv, int rotation, bool mirror);
Rect sampleRect(const Patch &p, const SampleEdit &e);
std::array<Point, 4> projectedRect(const Homography &h, const Geometry &g,
                                   Rect r);
int patchAt(const Geometry &g, const Homography &h, Point canonical);
double screenDistance(Point a, Point b, Point pixelScale);
} // namespace cm
