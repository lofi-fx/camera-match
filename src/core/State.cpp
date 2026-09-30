#include "State.h"
#include <cmath>
#include <iomanip>
#include <sstream>
namespace cm {
static uint64_t hash(const std::string &s) {
  uint64_t h = 14695981039346656037ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}
static void writeCapture(std::ostream &o, const Capture &c) {
  o << std::quoted(c.name) << ' ' << c.chartModel << ' ' << c.revision << ' '
    << c.time << ' ' << c.width << ' ' << c.height << ' ' << c.scaleX << ' '
    << c.scaleY << ' ' << c.par << ' ';
  for (auto v : c.bounds)
    o << v << ' ';
  o << c.geometry.model << ' ' << c.geometry.rotation << ' '
    << c.geometry.mirror << ' ';
  for (auto v : c.geometry.corners)
    o << v.x << ' ' << v.y << ' ';
  for (auto v : c.geometry.samples)
    o << v.dx << ' ' << v.dy << ' ' << v.w << ' ' << v.h << ' ';
  for (bool v : c.geometry.included)
    o << v << ' ';
  for (auto p : c.patch)
    o << p.rgb.r << ' ' << p.rgb.g << ' ' << p.rgb.b << ' ' << p.dispersion
      << ' ' << p.valid << ' ' << p.candidate << ' ' << p.flags << ' ';
}
static bool readCapture(std::istream &i, Capture &c) {
  if (!(i >> std::quoted(c.name) >> c.chartModel >> c.revision >> c.time >>
        c.width >> c.height >> c.scaleX >> c.scaleY >> c.par))
    return false;
  for (auto &v : c.bounds)
    if (!(i >> v))
      return false;
  int mirror = 0;
  if (!(i >> c.geometry.model >> c.geometry.rotation >> mirror) || mirror < 0 ||
      mirror > 1)
    return false;
  c.geometry.mirror = mirror;
  for (auto &v : c.geometry.corners)
    if (!(i >> v.x >> v.y) || !std::isfinite(v.x) || !std::isfinite(v.y))
      return false;
  for (auto &v : c.geometry.samples)
    if (!(i >> v.dx >> v.dy >> v.w >> v.h) || !std::isfinite(v.dx) ||
        !std::isfinite(v.dy) || !std::isfinite(v.w) || !std::isfinite(v.h))
      return false;
  for (auto &v : c.geometry.included) {
    int n;
    if (!(i >> n) || n < 0 || n > 1)
      return false;
    v = n;
  }
  for (auto &p : c.patch)
    if (!(i >> p.rgb.r >> p.rgb.g >> p.rgb.b >> p.dispersion >> p.valid >>
          p.candidate >> p.flags))
      return false;
  if (c.chartModel < 0 || c.chartModel > 1 ||
      c.geometry.model != c.chartModel || c.width < 0 || c.height < 0 ||
      !std::isfinite(c.time) || !std::isfinite(c.scaleX) ||
      !std::isfinite(c.scaleY) || !std::isfinite(c.par) || c.scaleX <= 0 ||
      c.scaleY <= 0 || c.par <= 0)
    return false;
  for (auto p : c.patch)
    if (!finite(p.rgb) || !std::isfinite(p.dispersion) || p.valid < 0 ||
        p.candidate < 0 || p.valid > p.candidate)
      return false;
  return true;
}
std::string serialize(const Persistent &s) {
  std::ostringstream o;
  o << std::setprecision(17) << "CM4 " << s.hasHero << ' ' << s.hasTarget
    << ' ';
  writeCapture(o, s.hero);
  writeCapture(o, s.target);
  auto x = s.solution;
  o << x.valid << ' ' << x.stops << ' ' << x.neutralLog.r << ' '
    << x.neutralLog.g << ' ' << x.neutralLog.b << ' ';
  for (double v : x.hue)
    o << v << ' ';
  for (double v : x.sat)
    o << v << ' ';
  o << x.neutralCount << ' ' << x.colorCount << ' ' << x.exposureMAD << ' '
    << int(x.method) << ' ' << x.radialCount;
  for (int j = 0; j < x.radialCount; ++j) {
    auto a = x.radial[j];
    o << ' ' << a.x << ' ' << a.y << ' ' << a.hue << ' ' << a.saturation
      << ' ' << a.weight;
  }
  o << ' ' << x.rbfCount << ' ' << x.rbfSupport;
  for (int j = 0; j < x.rbfCount; ++j) {
    auto c = x.rbfCenters[j], w = x.rbfWeights[j];
    o << ' ' << c.r << ' ' << c.g << ' ' << c.b << ' ' << w.r << ' ' << w.g
      << ' ' << w.b;
  }
  for (auto v : x.rbfAffine)
    o << ' ' << v.r << ' ' << v.g << ' ' << v.b;
  std::string body = o.str();
  std::ostringstream result;
  result << body << ' ' << hash(body);
  return result.str();
}
bool deserialize(const std::string &str, Persistent &out) {
  auto at = str.find_last_of(' ');
  if (at == std::string::npos)
    return false;
  std::string body = str.substr(0, at);
  uint64_t check = 0;
  try {
    check = std::stoull(str.substr(at + 1));
  } catch (...) {
    return false;
  }
  if (hash(body) != check)
    return false;
  std::istringstream i(body);
  std::string tag;
  Persistent s;
  int hero = 0, target = 0, valid = 0;
  if (!(i >> tag >> hero >> target) ||
      (tag != "CM2" && tag != "CM3" && tag != "CM4") ||
      hero < 0 || hero > 1 ||
      target < 0 || target > 1 || !readCapture(i, s.hero) ||
      !readCapture(i, s.target))
    return false;
  auto &x = s.solution;
  if (!(i >> valid >> x.stops >> x.neutralLog.r >> x.neutralLog.g >>
        x.neutralLog.b))
    return false;
  for (double &v : x.hue)
    if (!(i >> v))
      return false;
  for (double &v : x.sat)
    if (!(i >> v))
      return false;
  if (!(i >> x.neutralCount >> x.colorCount >> x.exposureMAD))
    return false;
  if (tag == "CM3" || tag == "CM4") {
    int method = 0;
    if (!(i >> method >> x.radialCount) || method < 0 ||
        method > (tag == "CM4" ? 2 : 1) ||
        x.radialCount < 0 || x.radialCount > int(x.radial.size()))
      return false;
    x.method = MatchMethod(method);
    for (int j = 0; j < x.radialCount; ++j) {
      auto &a = x.radial[j];
      if (!(i >> a.x >> a.y >> a.hue >> a.saturation >> a.weight) ||
          !std::isfinite(a.x) || !std::isfinite(a.y) ||
          !std::isfinite(a.hue) || !std::isfinite(a.saturation) ||
          !std::isfinite(a.weight) || a.weight < 0 || a.weight > 1)
        return false;
    }
  }
  if (tag == "CM4") {
    if (!(i >> x.rbfCount >> x.rbfSupport) || x.rbfCount < 0 ||
        x.rbfCount > int(x.rbfCenters.size()) ||
        !std::isfinite(x.rbfSupport))
      return false;
    for (int j = 0; j < x.rbfCount; ++j) {
      auto &c = x.rbfCenters[j], &w = x.rbfWeights[j];
      if (!(i >> c.r >> c.g >> c.b >> w.r >> w.g >> w.b) ||
          !finite(c) || !finite(w))
        return false;
    }
    for (auto &v : x.rbfAffine)
      if (!(i >> v.r >> v.g >> v.b) || !finite(v))
        return false;
    if (x.method == MatchMethod::Rbf &&
        (x.rbfCount < 7 || x.rbfSupport <= 0))
      return false;
  }
  std::string extra;
  if (i >> extra)
    return false;
  s.hasHero = hero;
  s.hasTarget = target;
  x.valid = valid;
  if ((!s.hasHero && s.hasTarget) || (x.valid && !s.hasTarget) ||
      !std::isfinite(x.stops) || !finite(x.neutralLog))
    return false;
  for (double v : x.hue)
    if (!std::isfinite(v))
      return false;
  for (double v : x.sat)
    if (!std::isfinite(v))
      return false;
  out = s;
  return true;
}
uint64_t fingerprint(const Capture &c) {
  Persistent s;
  s.hasHero = true;
  s.hero = c;
  return hash(serialize(s));
}
} // namespace cm
