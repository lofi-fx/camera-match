#include "PatchSampler.h"
#include "Color.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace cm {
uint32_t quality(const std::vector<RGB> &samples, double disp) {
  uint32_t f = 0;
  if (samples.size() < 16)
    f |= 1;
  if (disp > .1)
    f |= 2;
  return f;
}
Observation samplePatch(const SamplingImage &im, const Geometry &g,
                        const Homography &h, const Patch &p,
                        const SampleEdit &e, Point scale) {
  Observation o;
  auto box = sampleRect(p, e);
  auto projected = projectedRect(h, g, box);
  double minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30;
  for (auto q : projected) {
    minx = std::min(minx, q.x);
    maxx = std::max(maxx, q.x);
    miny = std::min(miny, q.y);
    maxy = std::max(maxy, q.y);
  }
  if (!std::isfinite(minx) || !std::isfinite(maxx) || !std::isfinite(miny) ||
      !std::isfinite(maxy) || scale.x <= 0 || scale.y <= 0 || im.par <= 0) {
    o.flags = 1;
    return o;
  }
  auto bounded = [](double v, int lo, int hi) {
    return int(std::clamp(v, double(lo), double(hi)));
  };
  int x1 =
      bounded(std::floor(minx * scale.x / im.par), im.pixels.x1, im.pixels.x2);
  int x2 =
      bounded(std::ceil(maxx * scale.x / im.par), im.pixels.x1, im.pixels.x2);
  int y1 = bounded(std::floor(miny * scale.y), im.pixels.y1, im.pixels.y2);
  int y2 = bounded(std::ceil(maxy * scale.y), im.pixels.y1, im.pixels.y2);
  std::vector<RGB> v;
  v.reserve(512);
  for (int y = y1; y < y2; y++)
    for (int x = x1; x < x2; x++) {
      Point uv = unorient(
          h.unproject({(x + .5) * im.par / scale.x, (y + .5) / scale.y}),
          g.rotation, g.mirror);
      if (uv.x < box.x1 || uv.x > box.x2 || uv.y < box.y1 || uv.y > box.y2)
        continue;
      o.candidate++;
      float *f = im.pixels.at(x, y);
      if (!f)
        continue;
      RGB rgb{f[0], f[1], f[2]};
      if (!finite(rgb))
        continue;
      if (im.pixels.components == 4 && f[3] < .5)
        continue;
      if (im.premult && im.pixels.components == 4 && f[3] > 1e-6f)
        rgb = rgb / double(f[3]);
      rgb = decode(rgb);
      if (finite(rgb))
        v.push_back(rgb);
    }
  if (v.empty()) {
    o.flags = 1;
    return o;
  }
  auto med = [](std::vector<double> &a) {
    std::sort(a.begin(), a.end());
    return a[a.size() / 2];
  };
  std::vector<double> lum;
  lum.reserve(v.size());
  for (auto x : v)
    lum.push_back(luminance(x));
  double center = med(lum), mad = 0;
  for (auto x : lum)
    mad += std::abs(x - center);
  mad /= lum.size();
  RGB sum{};
  double ds = 0;
  for (auto x : v)
    if (std::abs(luminance(x) - center) <= std::max(.003, 3 * mad)) {
      sum = sum + x;
      ds += std::abs(luminance(x) - center);
      o.valid++;
    }
  if (o.valid)
    o.rgb = sum / double(o.valid);
  o.dispersion = o.valid ? ds / o.valid : 0;
  o.flags = quality(v, o.dispersion);
  return o;
}
} // namespace cm
