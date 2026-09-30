#include "Chart.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace cm {
// Coordinates traced from the front-facing chart in Calibrite's ColorChecker
// Video Guide (2023), normalized to the printed target boundary x=233..548,
// y=104..315. Skin column: 6; narrow neutral ramp: 6; large gray blocks: 4;
// primary columns: 12; corner checks: 4.
const std::vector<Patch> &layout(int model) {
  if (model == 2) {
    static const std::vector<Patch> passport = []() {
      std::vector<Patch> a;
      static const char *ids[24] = {
          "Color 1", "Color 2", "Color 3", "Color 4", "Color 5", "Color 6",
          "Skin 1",  "Skin 2",  "Skin 3",  "Skin 4",  "Skin 5",  "Skin 6",
          "Gray 1",  "Gray 2",  "Gray 3",  "Gray 4",  "Gray 5",  "Gray 6",
          "Gray 7",  "Gray 8",  "Black",   "Gray 9",  "White 1", "White 2"};
      for (int row = 0; row < 4; row++)
        for (int col = 0; col < 6; col++) {
          double x = .065 + col * .148, y = .08 + row * .222;
          a.push_back({ids[row * 6 + col],
                       {x, y, x + .12, y + .17},
                       row == 0   ? Role::Chromatic
                       : row == 1 ? Role::Skin
                                  : Role::Neutral,
                       true});
        }
      return a;
    }();
    return passport;
  }
  if (model == 1) {
    static const std::vector<Patch> legacyPassport = []() {
      std::vector<Patch> a;
      static const char *ids[24] = {
          "Color 1", "Skin 1",  "Gray 1",  "Check 1", "Color 2",
          "Skin 2",  "Gray 2",  "Check 2", "Color 3", "Skin 3",
          "Gray 3",  "Check 3", "Color 4", "Skin 4", "Gray 4",
          "Check 4", "Color 5", "Skin 5",  "Gray 5", "Check 5",
          "Color 6", "Skin 6",  "Gray 6", "Glossy black"};
      for (int row = 0; row < 6; row++)
        for (int col = 0; col < 4; col++) {
          double x = .025 + col * .245, y = .018 + row * .164;
          a.push_back({ids[row * 4 + col],
                       {x, y, x + .205, y + .13},
                       col == 0   ? Role::Chromatic
                       : col == 1 ? Role::Skin
                       : col == 2 ? Role::Neutral
                       : row == 5 ? Role::Glossy
                                  : Role::Illumination,
                       col < 3});
        }
      return a;
    }();
    return legacyPassport;
  }
  static const std::vector<Patch> v = [] {
    std::vector<Patch> a;
    auto add = [&](std::string id, double x1, double y1, double x2, double y2,
                   Role r, bool on = true) {
      static std::vector<std::string> ids;
      ids.push_back(id);
      a.push_back({nullptr, {x1, y1, x2, y2}, r, on});
    };
    for (int i = 0; i < 6; i++)
      add("Skin " + std::to_string(i + 1), 0.006, 0.006 + i * .165, 0.09,
          0.145 + i * .165, Role::Skin);
    for (int i = 0; i < 6; i++)
      add("Gray " + std::to_string(i + 1), 0.105, 0.25 + i * .115, 0.18,
          0.35 + i * .115, Role::Neutral);
    for (int i = 0; i < 4; i++)
      add("Large gray " + std::to_string(i + 1), 0.205, 0.015 + i * .245, 0.79,
          0.235 + i * .245, Role::Neutral);
    for (int row = 0; row < 6; row++)
      for (int col = 0; col < 2; col++)
        add("Color " + std::to_string(row * 2 + col + 1), 0.805 + col * .095,
            0.14 + row * .139, 0.89 + col * .095, 0.27 + row * .139,
            Role::Chromatic);
    add("White check", .905, .005, .977, .12, Role::Illumination, false);
    add("Black check", .105, .005, .18, .12, Role::Glossy, false);
    // Stable strings are supplied separately below to avoid vector reallocation
    // of id storage.
    return a;
  }();
  static const std::array<std::string, 28> ids = []() {
    std::array<std::string, 28> a;
    int n = 0;
    for (int i = 0; i < 6; i++)
      a[n++] = "Skin " + std::to_string(i + 1);
    for (int i = 0; i < 6; i++)
      a[n++] = "Gray " + std::to_string(i + 1);
    for (int i = 0; i < 4; i++)
      a[n++] = "Large gray " + std::to_string(i + 1);
    for (int i = 0; i < 12; i++)
      a[n++] = "Color " + std::to_string(i + 1);
    return a;
  }();
  static const std::vector<Patch> fixed = []() {
    auto a = v;
    for (size_t i = 0; i < ids.size(); i++)
      a[i].id = ids[i].c_str();
    a[28].id = "White check";
    a[29].id = "Black check";
    return a;
  }();
  return fixed;
}
Geometry::Geometry() {
  auto &p = layout(model);
  for (size_t i = 0; i < p.size(); ++i)
    included[i] = p[i].defaultIncluded;
}
static double cross(Point a, Point b, Point c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool validCorners(const std::array<Point, 4> &c) {
  double sign = 0, area = 0;
  for (int i = 0; i < 4; i++) {
    auto a = c[i], b = c[(i + 1) % 4], d = c[(i + 2) % 4];
    if (!std::isfinite(a.x) || !std::isfinite(a.y) ||
        std::hypot(b.x - a.x, b.y - a.y) < 8)
      return false;
    double z = cross(a, b, d);
    if (std::abs(z) < 64)
      return false;
    if (i == 0)
      sign = z;
    else if (z * sign <= 0)
      return false;
    area += a.x * b.y - b.x * a.y;
  }
  return std::abs(area) > 256;
}
static bool invert(const double a[9], double b[9]) {
  double d = a[0] * (a[4] * a[8] - a[5] * a[7]) -
             a[1] * (a[3] * a[8] - a[5] * a[6]) +
             a[2] * (a[3] * a[7] - a[4] * a[6]);
  if (!std::isfinite(d) || std::abs(d) < 1e-12)
    return false;
  double x[9] = {a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8],
                 a[1] * a[5] - a[2] * a[4], a[5] * a[6] - a[3] * a[8],
                 a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
                 a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7],
                 a[0] * a[4] - a[1] * a[3]};
  for (int i = 0; i < 9; i++)
    b[i] = x[i] / d;
  return true;
}
Homography makeHomography(const Geometry &g) {
  Homography h;
  if (!validCorners(g.corners))
    return h;
  auto &c = g.corners;
  double dx1 = c[1].x - c[2].x, dx2 = c[3].x - c[2].x,
         dx3 = c[0].x - c[1].x + c[2].x - c[3].x;
  double dy1 = c[1].y - c[2].y, dy2 = c[3].y - c[2].y,
         dy3 = c[0].y - c[1].y + c[2].y - c[3].y;
  double den = dx1 * dy2 - dx2 * dy1;
  if (std::abs(den) < 1e-9)
    return h;
  double g1 = (dx3 * dy2 - dx2 * dy3) / den, g2 = (dx1 * dy3 - dx3 * dy1) / den;
  h.m[0] = c[1].x - c[0].x + g1 * c[1].x;
  h.m[1] = c[3].x - c[0].x + g2 * c[3].x;
  h.m[2] = c[0].x;
  h.m[3] = c[1].y - c[0].y + g1 * c[1].y;
  h.m[4] = c[3].y - c[0].y + g2 * c[3].y;
  h.m[5] = c[0].y;
  h.m[6] = g1;
  h.m[7] = g2;
  h.m[8] = 1;
  h.valid = invert(h.m, h.inv);
  for (auto p :
       std::array<Point, 5>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {.5, .5}}})
    if (std::abs(h.m[6] * p.x + h.m[7] * p.y + 1) < 1e-4)
      h.valid = false;
  return h;
}
Point Homography::project(Point p) const {
  double d = m[6] * p.x + m[7] * p.y + m[8];
  return {(m[0] * p.x + m[1] * p.y + m[2]) / d,
          (m[3] * p.x + m[4] * p.y + m[5]) / d};
}
Point Homography::unproject(Point p) const {
  double d = inv[6] * p.x + inv[7] * p.y + inv[8];
  return {(inv[0] * p.x + inv[1] * p.y + inv[2]) / d,
          (inv[3] * p.x + inv[4] * p.y + inv[5]) / d};
}
Point orient(Point p, int rotation, bool mirror) {
  if (mirror)
    p.x = 1 - p.x;
  switch ((rotation % 4 + 4) % 4) {
  case 1:
    return {1 - p.y, p.x};
  case 2:
    return {1 - p.x, 1 - p.y};
  case 3:
    return {p.y, 1 - p.x};
  default:
    return p;
  }
}
Point unorient(Point p, int rotation, bool mirror) {
  switch ((rotation % 4 + 4) % 4) {
  case 1:
    p = {p.y, 1 - p.x};
    break;
  case 2:
    p = {1 - p.x, 1 - p.y};
    break;
  case 3:
    p = {1 - p.y, p.x};
    break;
  }
  if (mirror)
    p.x = 1 - p.x;
  return p;
}
Rect sampleRect(const Patch &p, const SampleEdit &e) {
  double w = p.box.x2 - p.box.x1, h = p.box.y2 - p.box.y1,
         cx = (p.box.x1 + p.box.x2) / 2 + e.dx * w,
         cy = (p.box.y1 + p.box.y2) / 2 + e.dy * h;
  return {cx - e.w * w / 2, cy - e.h * h / 2, cx + e.w * w / 2,
          cy + e.h * h / 2};
}
std::array<Point, 4> projectedRect(const Homography &h, const Geometry &g,
                                   Rect r) {
  return {{h.project(orient({r.x1, r.y1}, g.rotation, g.mirror)),
           h.project(orient({r.x2, r.y1}, g.rotation, g.mirror)),
           h.project(orient({r.x2, r.y2}, g.rotation, g.mirror)),
           h.project(orient({r.x1, r.y2}, g.rotation, g.mirror))}};
}
int patchAt(const Geometry &g, const Homography &h, Point p) {
  if (!h.valid)
    return -1;
  auto q = unorient(h.unproject(p), g.rotation, g.mirror);
  auto &a = layout(g.model);
  for (size_t i = 0; i < a.size(); i++)
    if (q.x >= a[i].box.x1 && q.x <= a[i].box.x2 && q.y >= a[i].box.y1 &&
        q.y <= a[i].box.y2)
      return int(i);
  return -1;
}
double screenDistance(Point a, Point b, Point s) {
  return std::hypot((a.x - b.x) / std::max(s.x, 1e-9),
                    (a.y - b.y) / std::max(s.y, 1e-9));
}
} // namespace cm
