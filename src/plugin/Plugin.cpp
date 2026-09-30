#include "Chart.h"
#include "Color.h"
#include "HeroTransfer.h"
#include "ImageView.h"
#include "Match.h"
#include "PatchSampler.h"
#include "State.h"
#include "ofxCore.h"
#include "ofxDrawSuite.h"
#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxInteract.h"
#include "ofxKeySyms.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef __APPLE__
#include "MetalRender.h"
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#endif
using namespace cm;
namespace {
const OfxPropertySuiteV1 *prop = nullptr;
const OfxImageEffectSuiteV1 *fx = nullptr;
const OfxParameterSuiteV1 *params = nullptr;
const OfxInteractSuiteV1 *interact = nullptr;
const OfxDrawSuiteV1 *draw = nullptr;
#ifdef __APPLE__
constexpr bool useDrawSuite = false;
#else
constexpr bool useDrawSuite = true;
#endif
OfxHost *host = nullptr;
constexpr const char *id = "com.lofifx.CameraMatch";
constexpr const char *payload = "cameraMatchState";
constexpr const char *geoPayload = "chartGeometry";
HeroTransfer sessionHero;
std::mutex referenceNameMutex;
std::unordered_map<std::string, size_t> claimedReferenceNames;
thread_local std::unordered_map<std::string, OfxPropertySetHandle>
    descriptorParams;
struct Instance {
  std::atomic_bool changing{false};
  std::atomic<uint64_t> overlayRevision{0};
  std::mutex overlayMutex;
  std::vector<OfxInteractHandle> overlays;
  std::string claimedReferenceName;
  std::mutex renderMutex;
  struct RenderSnapshot {
    std::string payload;
    Solution solution;
    std::shared_ptr<const RadialLut> lut;
  };
  std::shared_ptr<const RenderSnapshot> renderSnapshot;
};
void registerOverlay(Instance *i, OfxInteractHandle h) {
  if (!i)
    return;
  std::lock_guard<std::mutex> lock(i->overlayMutex);
  i->overlays.push_back(h);
}
void unregisterOverlay(Instance *i, OfxInteractHandle h) {
  if (!i)
    return;
  std::lock_guard<std::mutex> lock(i->overlayMutex);
  auto &v = i->overlays;
  v.erase(std::remove(v.begin(), v.end(), h), v.end());
}
void redrawOverlays(Instance *i, bool diagnose = false) {
  if (!i || !interact)
    return;
  std::vector<OfxInteractHandle> handles;
  {
    std::lock_guard<std::mutex> lock(i->overlayMutex);
    handles = i->overlays;
  }
  for (auto h : handles) {
    OfxStatus result = interact->interactRedraw(h);
    if (diagnose)
      std::fprintf(stderr, "CameraMatch interactRedraw status=%d\n", result);
  }
}
struct EditGroup {
  OfxParamSetHandle ps;
  bool open;
  EditGroup(OfxParamSetHandle p, const char *name)
      : ps(p), open(params->paramEditBegin(p, name) == kOfxStatOK) {}
  ~EditGroup() {
    if (open)
      params->paramEditEnd(ps);
  }
};
struct ChangeScope {
  Instance *instance;
  ~ChangeScope() {
    if (instance)
      instance->changing = false;
  }
};
struct Interact {
  OfxImageEffectHandle effect = nullptr;
  uint64_t loggedRevision = UINT64_MAX;
  Point start{}, scale{1, 1};
  Geometry initial{}, last{};
  int kind = 0, index = -1, hover = -1;
  bool edit = false, moved = false;
};
OfxPropertySetHandle effectProps(OfxImageEffectHandle e) {
  OfxPropertySetHandle p = nullptr;
  fx->getPropertySet(e, &p);
  return p;
}
OfxParamSetHandle paramSet(OfxImageEffectHandle e) {
  OfxParamSetHandle p = nullptr;
  fx->getParamSet(e, &p);
  return p;
}
OfxParamHandle param(OfxParamSetHandle ps, const char *n) {
  OfxParamHandle p = nullptr;
  params->paramGetHandle(ps, n, &p, nullptr);
  return p;
}
double gd(OfxParamSetHandle ps, const char *n, double def = 0) {
  auto p = param(ps, n);
  double v = def;
  if (p)
    params->paramGetValue(p, &v);
  return v;
}
int gi(OfxParamSetHandle ps, const char *n, int def = 0) {
  auto p = param(ps, n);
  int v = def;
  if (p)
    params->paramGetValue(p, &v);
  return v;
}
std::string gs(OfxParamSetHandle ps, const char *n) {
  auto p = param(ps, n);
  char *s = nullptr;
  if (p)
    params->paramGetValue(p, &s);
  return s ? s : "";
}
void sd(OfxParamSetHandle ps, const char *n, double v) {
  auto p = param(ps, n);
  if (p)
    params->paramSetValue(p, v);
}
void si(OfxParamSetHandle ps, const char *n, int v) {
  auto p = param(ps, n);
  if (p)
    params->paramSetValue(p, v);
}
void ss(OfxParamSetHandle ps, const char *n, const std::string &v) {
  auto p = param(ps, n);
  if (p)
    params->paramSetValue(p, v.c_str());
}
Instance *instance(OfxImageEffectHandle e) {
  void *p = nullptr;
  prop->propGetPointer(effectProps(e), kOfxPropInstanceData, 0, &p);
  return static_cast<Instance *>(p);
}
void setInstance(OfxImageEffectHandle e, Instance *p) {
  prop->propSetPointer(effectProps(e), kOfxPropInstanceData, 0, p);
}
std::string cornerName(int i) { return "corner" + std::to_string(i); }
void xy(OfxParamSetHandle ps, const char *n, Point &v) {
  auto p = param(ps, n);
  if (p)
    params->paramGetValue(p, &v.x, &v.y);
}
void setXY(OfxParamSetHandle ps, const char *n, Point v) {
  auto p = param(ps, n);
  if (p)
    params->paramSetValue(p, v.x, v.y);
}
std::string encodeGeometry(const Geometry &g) {
  std::ostringstream o;
  o.precision(17);
  o << "G1 ";
  for (auto e : g.samples)
    o << e.dx << ' ' << e.dy << ' ' << e.w << ' ' << e.h << ' ';
  for (bool v : g.included)
    o << v << ' ';
  return o.str();
}
void decodeGeometry(const std::string &text, Geometry &g) {
  std::istringstream i(text);
  std::string tag;
  if (!(i >> tag) || tag != "G1")
    return;
  Geometry tmp = g;
  for (auto &e : tmp.samples)
    if (!(i >> e.dx >> e.dy >> e.w >> e.h) || e.w < .05 || e.w > 1 ||
        e.h < .05 || e.h > 1 || std::abs(e.dx) + e.w / 2 > .5 ||
        std::abs(e.dy) + e.h / 2 > .5)
      return;
  for (auto &v : tmp.included) {
    int n;
    if (!(i >> n) || n < 0 || n > 1)
      return;
    v = n;
  }
  g.samples = tmp.samples;
  g.included = tmp.included;
}
Geometry geometry(OfxParamSetHandle ps) {
  Geometry g;
  g.model = gi(ps, "chartModel") == 1 ? 2 : 0;
  auto &patches = layout(g.model);
  for (size_t j = 0; j < g.included.size(); ++j)
    g.included[j] = j < patches.size() ? patches[j].defaultIncluded : false;
  g.rotation = gi(ps, "rotation");
  g.mirror = gi(ps, "mirror") != 0;
  for (int j = 0; j < 4; j++)
    xy(ps, cornerName(j).c_str(), g.corners[j]);
  decodeGeometry(gs(ps, geoPayload), g);
  return g;
}
void writeGeometry(OfxParamSetHandle ps, const Geometry &g, bool corners) {
  if (corners)
    for (int j = 0; j < 4; j++)
      setXY(ps, cornerName(j).c_str(), g.corners[j]);
  else
    ss(ps, geoPayload, encodeGeometry(g));
}
Persistent state(OfxParamSetHandle ps) {
  Persistent s;
  deserialize(gs(ps, payload), s);
  return s;
}
bool commitState(OfxParamSetHandle ps, const Persistent &s) {
  auto handle = param(ps, payload);
  if (!handle)
    return false;
  const std::string previous = gs(ps, payload);
  const std::string next = serialize(s);
  if (params->paramSetValue(handle, next.c_str()) != kOfxStatOK ||
      gs(ps, payload) != next) {
    params->paramSetValue(handle, previous.c_str());
    return false;
  }
  return true;
}
void status(OfxParamSetHandle ps, const std::string &s) { ss(ps, "status", s); }
std::string trimName(std::string name) {
  auto nonspace = [](unsigned char c) { return !std::isspace(c); };
  name.erase(name.begin(), std::find_if(name.begin(), name.end(), nonspace));
  name.erase(std::find_if(name.rbegin(), name.rend(), nonspace).base(),
             name.end());
  return name;
}
void updateMatchControls(OfxParamSetHandle ps) {
  bool radial = gi(ps, "matchMethod") == 1;
  for (const char *name : {"biasWeight", "hue", "sat", "exposure", "neutral"}) {
    auto p = param(ps, name);
    OfxPropertySetHandle properties = nullptr;
    if (p && params->paramGetPropertySet(p, &properties) == kOfxStatOK)
      prop->propSetInt(properties, kOfxParamPropSecret, 0,
                       std::strcmp(name, "biasWeight") == 0 ? !radial : radial);
  }
}
MatchMethod selectedMatchMethod(OfxParamSetHandle ps) {
  return gi(ps, "matchMethod") == 1 ? MatchMethod::Rbf
                                     : MatchMethod::Harmonic;
}
void claimReferenceName(Instance *inst, const std::string &name) {
  std::lock_guard<std::mutex> lock(referenceNameMutex);
  if (inst->claimedReferenceName == name)
    return;
  if (!inst->claimedReferenceName.empty()) {
    auto it = claimedReferenceNames.find(inst->claimedReferenceName);
    if (it != claimedReferenceNames.end() && --it->second == 0)
      claimedReferenceNames.erase(it);
  }
  inst->claimedReferenceName = name;
  if (!name.empty())
    ++claimedReferenceNames[name];
}
std::string suggestReferenceName(Instance *inst) {
  std::lock_guard<std::mutex> lock(referenceNameMutex);
  for (unsigned n = 1;; ++n) {
    std::string name = "ref" + std::to_string(n);
    if (!claimedReferenceNames.count(name) && !sessionHero.find(name)) {
      claimedReferenceNames[name] = 1;
      inst->claimedReferenceName = name;
      return name;
    }
  }
}
std::string availableHeroes() {
  auto names = sessionHero.names();
  if (names.empty())
    return "No references captured this session";
  std::string result = "Available references: ";
  for (size_t j = 0; j < names.size(); ++j) {
    if (j)
      result += ", ";
    result += names[j];
  }
  return result;
}
void referenceStatus(OfxParamSetHandle ps, const Persistent &s) {
  if (!s.hasHero) {
    ss(ps, "referenceStatus",
       "No reference on this node. Enter Apply reference named, then apply.");
    return;
  }
  int count = 0;
  for (auto p : s.hero.patch)
    if (p.valid >= 16)
      count++;
  std::ostringstream o;
  o << s.hero.name << " · revision " << s.hero.revision << " · " << count
    << " patches · DWG/Intermediate · ID " << std::hex << fingerprint(s.hero);
  ss(ps, "referenceStatus", o.str());
}
void patchReport(OfxParamSetHandle ps) {
  auto s = state(ps);
  int j = gi(ps, "patchSelector");
  int model = gi(ps, "chartModel") == 1 ? 2 : 0;
  if (j < 0 || j >= int(layout(model).size())) {
    ss(ps, "patchReport", "No patch selected");
    return;
  }
  std::ostringstream o;
  o << layout(model)[j].id;
  if (s.hasHero) {
    auto p = s.hero.patch[j];
    o << " · reference " << p.valid << " pixels";
  }
  if (s.hasTarget) {
    auto p = s.target.patch[j];
    o << " · target " << p.valid << " pixels";
    if (p.valid && s.hero.patch[j].valid) {
      double y1 = luminance(s.hero.patch[j].rgb), y2 = luminance(p.rgb);
      if (y1 > 0 && y2 > 0) {
        o.setf(std::ios::fixed);
        o.precision(2);
        o << " · exposure " << std::log2(y1 / y2) << " stops";
      }
      bool radial = s.solution.method == MatchMethod::RadialLegacy;
      Amounts a{radial ? 1. : gd(ps, "hue", 100) / 100.,
                radial ? 1. : gd(ps, "sat", 100) / 100.,
                gd(ps, "exposure", 100) / 100., gd(ps, "neutral", 100) / 100.,
                false, gd(ps, "biasWeight", 100) / 100.};
      RGB after = decode(transform(encode(p.rgb), s.solution, a));
      Lab before = toOklab(p.rgb), fit = toOklab(after),
          hero = toOklab(s.hero.patch[j].rgb);
      if (before.L > 1e-5 && hero.L > 1e-5) {
        o << " · sat before " << std::hypot(before.a, before.b) / before.L
          << " after " << std::hypot(fit.a, fit.b) / std::max(fit.L, 1e-5);
      }
    }
  }
  ss(ps, "patchReport", o.str());
}
void define(OfxParamSetHandle ps, const char *type, const char *name,
            const char *label) {
  OfxPropertySetHandle p = nullptr;
  params->paramDefine(ps, type, name, &p);
  descriptorParams[name] = p;
  prop->propSetString(p, kOfxPropLabel, 0, label);
  prop->propSetInt(p, kOfxParamPropAnimates, 0, 0);
}
OfxPropertySetHandle desc(OfxParamSetHandle ps, const char *n) {
  auto found = descriptorParams.find(n);
  if (found != descriptorParams.end())
    return found->second;
  OfxParamHandle p = param(ps, n);
  OfxPropertySetHandle q = nullptr;
  if (p)
    params->paramGetPropertySet(p, &q);
  return q;
}
void parent(OfxParamSetHandle ps, const char *name, const char *group) {
  prop->propSetString(desc(ps, name), kOfxParamPropParent, 0, group);
}
void defaultDouble(OfxParamSetHandle ps, const char *n, double v, double lo,
                   double hi) {
  auto q = desc(ps, n);
  prop->propSetDouble(q, kOfxParamPropDefault, 0, v);
  prop->propSetDouble(q, kOfxParamPropMin, 0, lo);
  prop->propSetDouble(q, kOfxParamPropMax, 0, hi);
  prop->propSetDouble(q, kOfxParamPropDisplayMin, 0, lo);
  prop->propSetDouble(q, kOfxParamPropDisplayMax, 0, hi);
}
void choice(OfxParamSetHandle ps, const char *n, const char *label,
            std::initializer_list<const char *> opts) {
  define(ps, kOfxParamTypeChoice, n, label);
  auto q = desc(ps, n);
  int i = 0;
  for (auto x : opts)
    prop->propSetString(q, kOfxParamPropChoiceOption, i++, x);
  prop->propSetInt(q, kOfxParamPropDefault, 0, 0);
}
struct Image {
  OfxPropertySetHandle h = nullptr;
  void *data = nullptr;
  int stride = 0, components = 0;
  OfxRectI bounds{};
  double par = 1;
  bool premult = false;
  bool valid = false;
  Image() = default;
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;
  ~Image() {
    if (h)
      fx->clipReleaseImage(h);
  }
  bool fetch(OfxImageClipHandle clip, double time) {
    if (fx->clipGetImage(clip, time, nullptr, &h) != kOfxStatOK || !h)
      return false;
    char *depth = nullptr, *comp = nullptr;
    prop->propGetString(h, kOfxImageEffectPropPixelDepth, 0, &depth);
    prop->propGetString(h, kOfxImageEffectPropComponents, 0, &comp);
    if (!depth || strcmp(depth, kOfxBitDepthFloat))
      return false;
    components = comp && !strcmp(comp, kOfxImageComponentRGBA)  ? 4
                 : comp && !strcmp(comp, kOfxImageComponentRGB) ? 3
                                                                : 0;
    if (!components)
      return false;
    prop->propGetPointer(h, kOfxImagePropData, 0, &data);
    prop->propGetInt(h, kOfxImagePropRowBytes, 0, &stride);
    prop->propGetIntN(h, kOfxImagePropBounds, 4, &bounds.x1);
    prop->propGetDouble(h, kOfxImagePropPixelAspectRatio, 0, &par);
    char *prem = nullptr;
    prop->propGetString(h, kOfxImageEffectPropPreMultiplication, 0, &prem);
    premult = prem && !strcmp(prem, kOfxImagePreMultiplied);
    valid = data && stride && bounds.x2 > bounds.x1 && bounds.y2 > bounds.y1 &&
            par > 0;
    return valid;
  }
  float *at(int x, int y) const {
    if (!valid || x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 ||
        y >= bounds.y2)
      return nullptr;
    return FloatImageView{data,      bounds.x1, bounds.y1, bounds.x2,
                          bounds.y2, stride,    components}
        .at(x, y);
  }
};
OfxImageClipHandle clip(OfxImageEffectHandle e, const char *n) {
  OfxImageClipHandle c = nullptr;
  fx->clipGetHandle(e, n, &c, nullptr);
  return c;
}
bool capture(OfxImageEffectHandle e, OfxPropertySetHandle args,
             const Geometry &g, Capture &c, std::string &error) {
  auto h = makeHomography(g);
  if (!h.valid) {
    error = "Align four valid chart corners";
    return false;
  }
  double time = 0;
  prop->propGetDouble(args, kOfxPropTime, 0, &time);
  Image img;
  if (!img.fetch(clip(e, kOfxImageEffectSimpleSourceClipName), time)) {
    error =
        "Source image unavailable on CPU; disable GPU processing for capture";
    return false;
  }
  Point rs{};
  bool scaleKnown = prop->propGetDouble(img.h, kOfxImageEffectPropRenderScale,
                                        0, &rs.x) == kOfxStatOK &&
                    prop->propGetDouble(img.h, kOfxImageEffectPropRenderScale,
                                        1, &rs.y) == kOfxStatOK;
  if (!scaleKnown)
    scaleKnown = prop->propGetDouble(args, kOfxImageEffectPropRenderScale, 0,
                                     &rs.x) == kOfxStatOK &&
                 prop->propGetDouble(args, kOfxImageEffectPropRenderScale, 1,
                                     &rs.y) == kOfxStatOK;
  if (!scaleKnown) {
    OfxRectD rod{};
    if (fx->clipGetRegionOfDefinition(
            clip(e, kOfxImageEffectSimpleSourceClipName), time, &rod) ==
            kOfxStatOK &&
        std::isfinite(rod.x1) && std::isfinite(rod.x2) &&
        std::isfinite(rod.y1) && std::isfinite(rod.y2) && rod.x2 > rod.x1 &&
        rod.y2 > rod.y1) {
      rs.x = (img.bounds.x2 - img.bounds.x1) * img.par / (rod.x2 - rod.x1);
      rs.y = (img.bounds.y2 - img.bounds.y1) / (rod.y2 - rod.y1);
      scaleKnown = true;
    }
  }
  if (!scaleKnown || !std::isfinite(rs.x) || !std::isfinite(rs.y) ||
      rs.x <= 0 || rs.y <= 0) {
    error = "Cannot determine capture render scale";
    return false;
  }
  c.chartModel = g.model;
  c.geometry = g;
  c.bounds = {img.bounds.x1, img.bounds.y1, img.bounds.x2, img.bounds.y2};
  c.time = time;
  c.width = img.bounds.x2 - img.bounds.x1;
  c.height = img.bounds.y2 - img.bounds.y1;
  c.scaleX = rs.x;
  c.scaleY = rs.y;
  c.par = img.par;
  auto &patches = layout(g.model);
  int good = 0;
  SamplingImage sampled{{img.data, img.bounds.x1, img.bounds.y1, img.bounds.x2,
                         img.bounds.y2, img.stride, img.components},
                        img.par,
                        img.premult};
  for (size_t i = 0; i < patches.size(); i++) {
    c.patch[i] = samplePatch(sampled, g, h, patches[i], g.samples[i], rs);
    if (c.patch[i].valid >= 16)
      good++;
  }
  if (good < 7) {
    error = "Too few usable patches; enlarge or realign the chart";
    return false;
  }
  return true;
}
OfxStatus render(OfxImageEffectHandle e, OfxPropertySetHandle args) {
  double time = 0;
  prop->propGetDouble(args, kOfxPropTime, 0, &time);
  Image src, dst;
  if (!src.fetch(clip(e, kOfxImageEffectSimpleSourceClipName), time) ||
      !dst.fetch(clip(e, kOfxImageEffectOutputClipName), time))
    return kOfxStatFailed;
  auto ps = paramSet(e);
  auto *inst = instance(e);
  std::shared_ptr<const Instance::RenderSnapshot> snapshot;
  std::string saved = gs(ps, payload);
  {
    std::lock_guard<std::mutex> lock(inst->renderMutex);
    if (!inst->renderSnapshot || inst->renderSnapshot->payload != saved) {
      auto next = std::make_shared<Instance::RenderSnapshot>();
      next->payload = saved;
      Persistent parsed;
      if (deserialize(saved, parsed))
        next->solution = parsed.solution;
      if (next->solution.method == MatchMethod::RadialLegacy &&
          next->solution.radialCount > 0)
        next->lut = std::make_shared<RadialLut>(makeRadialLut(next->solution));
      inst->renderSnapshot = next;
    }
    snapshot = inst->renderSnapshot;
  }
  const auto &s = snapshot->solution;
  bool radial = s.method == MatchMethod::RadialLegacy;
  Amounts a{radial ? 1. : gd(ps, "hue", 100) / 100.,
            radial ? 1. : gd(ps, "sat", 100) / 100.,
            gd(ps, "exposure", 100) / 100., gd(ps, "neutral", 100) / 100.,
            gi(ps, "bypass") != 0, gd(ps, "biasWeight", 100) / 100.};
  bool exactCopy =
      (a.bypass || !s.valid ||
       (s.method == MatchMethod::Rbf
            ? a.biasWeight == 0
            : ((a.hue == 0 && a.sat == 0 ||
                (s.method == MatchMethod::RadialLegacy && a.biasWeight == 0)) &&
               a.exposure == 0 && a.neutral == 0))) &&
      src.components == dst.components && src.premult == dst.premult;
  OfxRectI win = dst.bounds;
  prop->propGetIntN(args, kOfxImageEffectPropRenderWindow, 4, &win.x1);
#ifdef __APPLE__
  int metalEnabled = 0;
  if (prop->propGetInt(args, kOfxImageEffectPropMetalEnabled, 0,
                       &metalEnabled) == kOfxStatOK && metalEnabled) {
    void *queue = nullptr;
    prop->propGetPointer(args, kOfxImageEffectPropMetalCommandQueue, 0,
                         &queue);
    MetalParams m{};
    for (int j = 0; j < 3; ++j) {
      m.hue[j] = float(s.hue[j]);
      m.sat[j] = float(s.sat[j]);
    }
    for (int j = 0; j < std::min(s.rbfCount, 32); ++j) {
      const RGB &center = s.rbfCenters[j], &weight = s.rbfWeights[j];
      m.rbfCenter[j][0] = float(center.r);
      m.rbfCenter[j][1] = float(center.g);
      m.rbfCenter[j][2] = float(center.b);
      m.rbfWeight[j][0] = float(weight.r);
      m.rbfWeight[j][1] = float(weight.g);
      m.rbfWeight[j][2] = float(weight.b);
    }
    for (int j = 0; j < 4; ++j) {
      const RGB &affine = s.rbfAffine[j];
      m.rbfAffine[j][0] = float(affine.r);
      m.rbfAffine[j][1] = float(affine.g);
      m.rbfAffine[j][2] = float(affine.b);
    }
    m.rbfCount = std::min(s.rbfCount, 32);
    m.rbfInvSupportSq = s.rbfSupport > 0
                            ? float(1. / (s.rbfSupport * s.rbfSupport))
                            : 0.f;
    m.neutral[0] = float(s.neutralLog.r);
    m.neutral[1] = float(s.neutralLog.g);
    m.neutral[2] = float(s.neutralLog.b);
    m.stops = float(s.stops);
    m.hueAmount = float(a.hue);
    m.satAmount = float(a.sat);
    m.exposureAmount = float(a.exposure);
    m.neutralAmount = float(a.neutral);
    m.biasWeight = float(a.biasWeight);
    m.method = int(s.method);
    m.srcX = src.bounds.x1; m.srcY = src.bounds.y1;
    m.srcW = src.bounds.x2 - src.bounds.x1;
    m.srcH = src.bounds.y2 - src.bounds.y1;
    m.dstX = dst.bounds.x1; m.dstY = dst.bounds.y1;
    m.dstW = dst.bounds.x2 - dst.bounds.x1;
    m.dstH = dst.bounds.y2 - dst.bounds.y1;
    m.winX = win.x1; m.winY = win.y1;
    m.winW = win.x2 - win.x1; m.winH = win.y2 - win.y1;
    m.srcRowFloats = src.stride / int(sizeof(float));
    m.dstRowFloats = dst.stride / int(sizeof(float));
    m.srcComponents = src.components;
    m.dstComponents = dst.components;
    m.srcPremult = src.premult;
    m.dstPremult = dst.premult;
    m.exactCopy = exactCopy;
    return renderMetal(queue, src.data, dst.data, m, snapshot->lut.get()) ? kOfxStatOK
                                                    : kOfxStatGPURenderFailed;
  }
#endif
  for (int y = std::max(win.y1, dst.bounds.y1);
       y < std::min(win.y2, dst.bounds.y2); y++)
    for (int x = std::max(win.x1, dst.bounds.x1);
         x < std::min(win.x2, dst.bounds.x2); x++) {
      float *d = dst.at(x, y), *p = src.at(x, y);
      if (!d)
        continue;
      if (!p) {
        for (int k = 0; k < dst.components; k++)
          d[k] = 0;
        continue;
      }
      if (exactCopy) {
        std::memcpy(d, p, src.components * sizeof(float));
        continue;
      }
      RGB rgb{p[0], p[1], p[2]};
      double alpha = src.components == 4 ? p[3] : 1.0;
      if (src.premult && alpha > 1e-6)
        rgb = rgb / alpha;
      RGB z = transform(rgb, s, a, snapshot->lut.get());
      if (dst.premult)
        z = z * alpha;
      d[0] = float(z.r);
      d[1] = float(z.g);
      d[2] = float(z.b);
      if (dst.components == 4)
        d[3] = float(alpha);
    }
  return kOfxStatOK;
}
Point propPoint(OfxPropertySetHandle p, const char *n, Point def = {}) {
  Point v = def;
  if (p) {
    prop->propGetDouble(p, n, 0, &v.x);
    prop->propGetDouble(p, n, 1, &v.y);
  }
  return v;
}
Interact *getInteract(OfxInteractHandle h) {
  OfxPropertySetHandle p = nullptr;
  interact->interactGetPropertySet(h, &p);
  void *v = nullptr;
  prop->propGetPointer(p, kOfxPropInstanceData, 0, &v);
  return static_cast<Interact *>(v);
}
void putInteract(OfxInteractHandle h, Interact *i) {
  OfxPropertySetHandle p = nullptr;
  interact->interactGetPropertySet(h, &p);
  prop->propSetPointer(p, kOfxPropInstanceData, 0, i);
}
void finish(Interact *i, bool cancel) {
  if (!i)
    return;
  auto ps = paramSet(i->effect);
  if (cancel && i->edit)
    writeGeometry(ps, i->initial, i->kind == 1 || i->kind == 2);
  if (i->edit) {
    params->paramEditEnd(ps);
    i->edit = false;
  }
  i->kind = 0;
  i->index = -1;
  i->moved = false;
}
void line(OfxDrawContextHandle d, Point a, Point b, const OfxRGBAColourF &c) {
  if (d && draw) {
    draw->setColour(d, &c);
    OfxPointD p[2] = {{a.x, a.y}, {b.x, b.y}};
    draw->draw(d, kOfxDrawPrimitiveLines, p, 2);
  }
#ifdef __APPLE__
  else {
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_LINES);
    glVertex2d(a.x, a.y);
    glVertex2d(b.x, b.y);
    glEnd();
  }
#endif
}
void loop(OfxDrawContextHandle d, const std::array<Point, 4> &v,
          const OfxRGBAColourF &c,
          OfxDrawLineStipplePattern stipple = kOfxDrawLineStipplePatternSolid) {
  if (d && draw) {
    draw->setColour(d, &c);
    draw->setLineStipple(d, stipple);
    OfxPointD p[4];
    for (int j = 0; j < 4; j++)
      p[j] = {v[j].x, v[j].y};
    draw->draw(d, kOfxDrawPrimitiveLineLoop, p, 4);
    draw->setLineStipple(d, kOfxDrawLineStipplePatternSolid);
  }
#ifdef __APPLE__
  else {
    glColor4f(c.r, c.g, c.b, c.a);
    if (stipple != kOfxDrawLineStipplePatternSolid) {
      glEnable(GL_LINE_STIPPLE);
      glLineStipple(1, stipple == kOfxDrawLineStipplePatternDot ? 0x1111 : 0x00ff);
    }
    glBegin(GL_LINE_LOOP);
    for (auto p : v)
      glVertex2d(p.x, p.y);
    glEnd();
    glDisable(GL_LINE_STIPPLE);
  }
#endif
}
void handle(OfxDrawContextHandle d, Point p, Point s, const OfxRGBAColourF &c) {
  std::array<Point, 4> a{{{p.x - 5 * s.x, p.y - 5 * s.y},
                          {p.x + 5 * s.x, p.y - 5 * s.y},
                          {p.x + 5 * s.x, p.y + 5 * s.y},
                          {p.x - 5 * s.x, p.y + 5 * s.y}}};
  const OfxRGBAColourF black{0, 0, 0, .9f};
  loop(d, a, black);
  std::array<Point, 4> b{{{p.x - 4 * s.x, p.y - 4 * s.y},
                          {p.x + 4 * s.x, p.y - 4 * s.y},
                          {p.x + 4 * s.x, p.y + 4 * s.y},
                          {p.x - 4 * s.x, p.y + 4 * s.y}}};
  loop(d, b, c);
}
enum class OverlayResult { Drawn, Hidden, NoContext, InvalidGeometry };
OverlayResult drawOverlay(Interact *i, OfxPropertySetHandle args) {
  if (!i || !gi(paramSet(i->effect), "showOverlay", 1))
    return OverlayResult::Hidden;
  void *p = nullptr;
  if (useDrawSuite)
    prop->propGetPointer(args, kOfxInteractPropDrawContext, 0, &p);
  auto d = static_cast<OfxDrawContextHandle>(p);
#ifndef __APPLE__
  if (!d || !draw)
    return OverlayResult::NoContext;
#else
  if (!d && !CGLGetCurrentContext())
    return OverlayResult::NoContext;
#endif
  auto ps = paramSet(i->effect);
  auto g = geometry(ps);
  auto h = makeHomography(g);
  if (!h.valid)
    return OverlayResult::InvalidGeometry;
#ifdef __APPLE__
  if (!d)
    glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_CURRENT_BIT);
#endif
  Point scale = propPoint(args, kOfxInteractPropPixelScale, {1, 1});
  const OfxRGBAColourF white{1, 1, 1, .95f}, black{0, 0, 0, .9f},
      blue{.1f, .75f, 1, .95f}, warn{1, .5f, .15f, .95f};
  if (d && draw)
    draw->setLineWidth(d, 1.5f);
#ifdef __APPLE__
  else
    glLineWidth(1.5f);
#endif
  loop(d, g.corners, black);
  auto outline = g.corners;
  for (auto &v : outline) {
    v.x += scale.x;
    v.y += scale.y;
  }
  loop(d, outline, white);
  for (int j = 0; j < 4; j++) {
    handle(d, g.corners[j], scale, blue);
    OfxPointD pos{g.corners[j].x + 8 * scale.x, g.corners[j].y + 8 * scale.y};
    const char *labels[] = {"TL", "TR", "BR", "BL"};
    if (d && draw) {
      draw->setColour(d, &white);
      draw->drawText(d, labels[j], &pos, kOfxDrawTextAlignmentLeft);
    }
  }
  auto &patches = layout(g.model);
  int selected = gi(ps, "patchSelector");
  for (size_t j = 0; j < patches.size(); j++) {
    auto rect = projectedRect(h, g, patches[j].box);
    loop(d, rect, black);
    loop(d, rect,
         int(j) == i->hover || int(j) == selected ? blue
         : g.included[j]                          ? white
                                                  : warn,
         g.included[j] ? kOfxDrawLineStipplePatternSolid
                       : kOfxDrawLineStipplePatternDash);
    auto sample = projectedRect(h, g, sampleRect(patches[j], g.samples[j]));
    loop(d, sample, g.included[j] ? blue : warn, kOfxDrawLineStipplePatternDot);
    if (!g.included[j]) {
      line(d, rect[0], rect[2], warn);
      line(d, rect[1], rect[3], warn);
    }
    if (gi(ps, "editMode") == 2 && int(j) == selected) {
      Point center{(sample[0].x + sample[2].x) / 2,
                   (sample[0].y + sample[2].y) / 2};
      handle(d, center, scale, blue);
      for (auto q : sample)
        handle(d, q, scale, white);
    }
  }
  if (gi(ps, "editMode") == 0) {
    auto center = h.project({.5, .5});
    handle(d, center, scale, blue);
  }
#ifdef __APPLE__
  if (!d)
    glPopAttrib();
#endif
  return OverlayResult::Drawn;
}
void refreshSelected(OfxParamSetHandle ps, const Geometry &g, int j) {
  if (j < 0 || j >= int(layout(g.model).size()))
    return;
  si(ps, "included", g.included[j] ? 1 : 0);
  sd(ps, "sampleX", g.samples[j].dx);
  sd(ps, "sampleY", g.samples[j].dy);
  sd(ps, "sampleW", g.samples[j].w);
  sd(ps, "sampleH", g.samples[j].h);
}
void beginEdit(Interact *i) {
  if (!i->edit) {
    params->paramEditBegin(paramSet(i->effect), "Camera Match chart edit");
    i->edit = true;
  }
}
OfxStatus overlayMain(const char *action, const void *handle,
                      OfxPropertySetHandle args, OfxPropertySetHandle) {
  auto h = (OfxInteractHandle)handle;
  Interact *active = nullptr;
  try {
    if (!strcmp(action, kOfxActionDescribe)) {
      OfxPropertySetHandle p = nullptr;
      interact->interactGetPropertySet(h, &p);
      const char *slaves[] = {"corner0",  "corner1",      "corner2",
                              "corner3",  geoPayload,     payload,
                              "chartModel", "rotation",   "mirror",
                              "showOverlay", "editMode", "patchSelector"};
      for (int j = 0; j < 12; j++)
        prop->propSetString(p, kOfxInteractPropSlaveToParam, j, slaves[j]);
      return kOfxStatOK;
    }
    if (!strcmp(action, kOfxActionCreateInstance)) {
      auto *i = new Interact;
      OfxPropertySetHandle p = nullptr;
      interact->interactGetPropertySet(h, &p);
      const char *slaves[] = {"corner0",  "corner1",      "corner2",
                              "corner3",  geoPayload,     payload,
                              "chartModel", "rotation",   "mirror",
                              "showOverlay", "editMode", "patchSelector"};
      for (int j = 0; j < 12; j++)
        prop->propSetString(p, kOfxInteractPropSlaveToParam, j, slaves[j]);
      void *e = nullptr;
      prop->propGetPointer(p, kOfxPropEffectInstance, 0, &e);
      i->effect = (OfxImageEffectHandle)e;
      putInteract(h, i);
      registerOverlay(instance(i->effect), h);
      std::fprintf(stderr, "CameraMatch overlay instance created\n");
      return kOfxStatOK;
    }
    auto *i = getInteract(h);
    active = i;
    if (!i)
      return kOfxStatReplyDefault;
    if (!strcmp(action, kOfxActionDestroyInstance)) {
      finish(i, false);
      unregisterOverlay(instance(i->effect), h);
      std::fprintf(stderr, "CameraMatch overlay instance destroyed\n");
      putInteract(h, nullptr);
      delete i;
      return kOfxStatOK;
    }
    if (!strcmp(action, kOfxInteractActionDraw)) {
      auto result = drawOverlay(i, args);
      if (auto *effectInstance = instance(i->effect)) {
        uint64_t revision = effectInstance->overlayRevision.load();
        if (i->loggedRevision != revision) {
          std::fprintf(stderr,
                       "CameraMatch overlay draw revision=%llu result=%d\n",
                       static_cast<unsigned long long>(revision), int(result));
          i->loggedRevision = revision;
        }
      }
      return kOfxStatOK;
    }
    if (!strcmp(action, kOfxInteractActionLoseFocus)) {
      finish(i, false);
      i->hover = -1;
      return kOfxStatReplyDefault;
    }
    if (!strcmp(action, kOfxInteractActionKeyDown)) {
      char *s = nullptr;
      prop->propGetString(args, kOfxPropKeyString, 0, &s);
      if (i->kind && s && s[0] == 27) {
        finish(i, true);
        interact->interactRedraw(h);
        return kOfxStatOK;
      }
      return kOfxStatReplyDefault;
    }
    if (strcmp(action, kOfxInteractActionPenDown) &&
        strcmp(action, kOfxInteractActionPenMotion) &&
        strcmp(action, kOfxInteractActionPenUp))
      return kOfxStatReplyDefault;
    auto ps = paramSet(i->effect);
    if (!gi(ps, "showOverlay", 1))
      return kOfxStatReplyDefault;
    Geometry g = geometry(ps);
    auto hom = makeHomography(g);
    if (!hom.valid)
      return kOfxStatReplyDefault;
    Point pos = propPoint(args, kOfxInteractPropPenPosition),
          scale = propPoint(args, kOfxInteractPropPixelScale, {1, 1});
    int mode = gi(ps, "editMode");
    auto &patches = layout(g.model);
    if (!strcmp(action, kOfxInteractActionPenDown)) {
      finish(i, false);
      i->initial = g;
      i->last = g;
      i->start = pos;
      i->scale = scale;
      int kind = 0, index = -1;
      if (mode == 0) {
        for (int j = 0; j < 4; j++)
          if (screenDistance(pos, g.corners[j], scale) <= 10) {
            kind = 1;
            index = j;
            break;
          }
        if (!kind && screenDistance(pos, hom.project({.5, .5}), scale) <= 10)
          kind = 2;
      } else if (mode == 1) {
        index = patchAt(g, hom, pos);
        if (index >= 0)
          kind = 3;
      } else if (mode == 2) {
        for (size_t j = 0; j < patches.size() && !kind; j++) {
          auto r = projectedRect(hom, g, sampleRect(patches[j], g.samples[j]));
          for (int k = 0; k < 4; k++)
            if (screenDistance(pos, r[k], scale) <= 9) {
              kind = 5;
              index = int(j) * 4 + k;
              break;
            }
          if (!kind) {
            Point center{(r[0].x + r[2].x) / 2, (r[0].y + r[2].y) / 2};
            if (screenDistance(pos, center, scale) <= 10) {
              kind = 4;
              index = int(j);
            }
          }
        }
        if (!kind) {
          index = patchAt(g, hom, pos);
          if (index >= 0)
            kind = 3;
        }
      }
      if (!kind)
        return kOfxStatReplyDefault;
      i->kind = kind;
      i->index = index;
      if (kind == 1 || kind == 2 || kind == 4 || kind == 5)
        beginEdit(i);
      return kOfxStatOK;
    }
    if (!strcmp(action, kOfxInteractActionPenMotion)) {
      if (!i->kind) {
        int hit = patchAt(g, hom, pos);
        if (hit != i->hover) {
          i->hover = hit;
          interact->interactRedraw(h);
        }
        return kOfxStatReplyDefault;
      }
      if (screenDistance(pos, i->start, scale) > 3)
        i->moved = true;
      if (i->kind == 3)
        return kOfxStatOK;
      Geometry proposed = i->initial;
      Point delta{pos.x - i->start.x, pos.y - i->start.y};
      if (i->kind == 1) {
        proposed.corners[i->index].x += delta.x;
        proposed.corners[i->index].y += delta.y;
      } else if (i->kind == 2)
        for (auto &c : proposed.corners) {
          c.x += delta.x;
          c.y += delta.y;
        }
      else {
        int idx = i->kind == 5 ? i->index / 4 : i->index;
        if (idx < 0 || idx >= int(patches.size()))
          return kOfxStatOK;
        Point uv0 = unorient(hom.unproject(i->start), g.rotation, g.mirror),
              uv1 = unorient(hom.unproject(pos), g.rotation, g.mirror);
        auto box = patches[idx].box;
        double dx = (uv1.x - uv0.x) / (box.x2 - box.x1),
               dy = (uv1.y - uv0.y) / (box.y2 - box.y1);
        auto &e = proposed.samples[idx];
        if (i->kind == 4) {
          e.dx += dx;
          e.dy += dy;
        } else {
          int corner = i->index % 4;
          double sx = corner == 0 || corner == 3 ? -1 : 1,
                 sy = corner < 2 ? -1 : 1;
          e.w += sx * dx;
          e.h += sy * dy;
          e.dx += dx / 2;
          e.dy += dy / 2;
        }
        if (e.w < .1 || e.h < .1 || std::abs(e.dx) + e.w / 2 > .5 ||
            std::abs(e.dy) + e.h / 2 > .5)
          return kOfxStatOK;
      }
      if (!makeHomography(proposed).valid)
        return kOfxStatOK;
      writeGeometry(ps, proposed, i->kind == 1 || i->kind == 2);
      i->last = proposed;
      interact->interactRedraw(h);
      return kOfxStatOK;
    }
    if (i->kind == 3 && !i->moved && patchAt(g, hom, pos) == i->index) {
      beginEdit(i);
      int j = i->index;
      si(ps, "patchSelector", j);
      if (mode == 1) {
        g.included[j] = !g.included[j];
        writeGeometry(ps, g, false);
      }
      auto *inst = instance(i->effect);
      if (inst)
        inst->changing = true;
      refreshSelected(ps, g, j);
      patchReport(ps);
      if (inst)
        inst->changing = false;
    }
    bool handled = i->kind != 0;
    finish(i, false);
    interact->interactRedraw(h);
    return handled ? kOfxStatOK : kOfxStatReplyDefault;
  } catch (...) {
    finish(active, false);
    return kOfxStatFailed;
  }
}
OfxStatus changed(OfxImageEffectHandle e, OfxPropertySetHandle args) {
  auto *inst = instance(e);
  if (!inst || inst->changing)
    return kOfxStatOK;
  char *n = nullptr;
  prop->propGetString(args, kOfxPropName, 0, &n);
  if (!n)
    return kOfxStatOK;
  std::string name = n;
  auto ps = paramSet(e);
  if (name == "heroName") {
    claimReferenceName(inst, trimName(gs(ps, "heroName")));
    return kOfxStatOK;
  }
  inst->changing = true;
  ChangeScope changeScope{inst};
  auto done = [&]() {
    inst->changing = false;
    inst->overlayRevision.fetch_add(1);
    if (name == "captureHero" || name == "analyze" ||
        name == "showOverlay") {
      std::lock_guard<std::mutex> lock(inst->overlayMutex);
      std::fprintf(stderr, "CameraMatch parameter %s: %zu overlays active\n",
                   name.c_str(), inst->overlays.size());
    }
    redrawOverlays(inst, name == "showOverlay");
    return kOfxStatOK;
  };
  if (name == "matchMethod") {
    updateMatchControls(ps);
    auto s = state(ps);
    if (s.hasHero && s.hasTarget) {
      if (s.hero.chartModel == 1 && geometry(ps).model == 2) {
        status(ps, "Passport patch layout was corrected. Recapture the reference and target.");
        return done();
      }
      auto fit = solve(s.hero, s.target, geometry(ps), selectedMatchMethod(ps));
      if (!fit.solution.valid) {
        status(ps, fit.error);
        return done();
      }
      s.solution = fit.solution;
      EditGroup edit(ps, "Change match method");
      if (!commitState(ps, s)) {
        status(ps, "Could not save the selected match method");
        return done();
      }
      status(ps, std::string("Applied ") +
                     (fit.solution.method == MatchMethod::Rbf
                          ? "RBF match"
                          : "existing match") +
                     (fit.error.empty() ? "" : " · " + fit.error));
      patchReport(ps);
    } else {
      status(ps, "Match method selected. Apply a reference to this clip.");
    }
    return done();
  }
  if (name == "showOverlay") {
    status(ps, gi(ps, "showOverlay")
                   ? "Overlay requested. If guides stay hidden, select Open FX Overlay beneath the viewer."
                   : "Overlay hidden");
    return done();
  }
  if (name == "listHeroes") {
    status(ps, availableHeroes());
    return done();
  }
  if (name == "makeHeroAvailable") {
    Persistent s = state(ps);
    if (!s.hasHero) {
      status(ps, "Capture a reference on this node first");
    } else if (trimName(s.hero.name).empty() &&
               trimName(gs(ps, "heroName")).empty()) {
      status(ps, "Enter a unique name in Save reference as to register it");
    } else {
      Capture registered = s.hero;
      std::string alias = trimName(gs(ps, "heroName"));
      if (!alias.empty())
        registered.name = alias;
      auto existing = sessionHero.find(registered.name);
      if (existing && fingerprint(*existing) != fingerprint(registered))
        status(ps, "Reference name already registered for another capture: " +
                       registered.name);
      else {
        sessionHero.publish(registered);
        status(ps, "Reference ready for other clips: " + registered.name);
      }
    }
    return done();
  }
  if (name == "captureHero" || name == "analyze") {
    Persistent s = state(ps);
    if (name == "captureHero") {
      std::string requested = trimName(gs(ps, "heroName"));
      if (requested.empty()) {
        status(ps, "Enter a unique reference name, such as Scene 1");
        return done();
      }
      auto existing = sessionHero.find(requested);
      Capture previous = s.hero;
      previous.name = requested;
      if (existing &&
          (s.hasTarget || !s.hasHero ||
           fingerprint(*existing) != fingerprint(previous))) {
        status(ps, "Reference name already registered. Use a unique scene name: " +
                       requested);
        return done();
      }
    } else {
      std::string requested = trimName(gs(ps, "heroToApply"));
      if (!requested.empty()) {
        auto selected = sessionHero.find(requested);
        if (selected) {
          s.hero = *selected;
          s.hasHero = true;
        } else if (!s.hasHero || s.hero.name != requested) {
          status(ps, "Reference '" + requested + "' not found. " +
                         availableHeroes());
          return done();
        }
      } else if (!s.hasHero) {
        status(ps, "Enter Apply reference named. " + availableHeroes());
        return done();
      }
    }
    Geometry g = geometry(ps);
    if (name == "analyze" && s.hero.chartModel != g.model) {
      status(ps, s.hero.chartModel == 1 && g.model == 2
                     ? "Passport patch layout was corrected. Recapture the reference before applying."
                     : "Reference chart model differs. Select the same chart model before applying.");
      return done();
    }
    Capture c;
    std::string error;
    if (!capture(e, args, g, c, error)) {
      status(ps, error);
      return done();
    }
    std::string resultStatus;
    if (name == "captureHero") {
      c.name = trimName(gs(ps, "heroName"));
      c.revision = s.hasHero ? s.hero.revision + 1 : 1;
      s.hero = c;
      s.hasHero = true;
      s.hasTarget = false;
      s.solution = {};
      std::ostringstream o;
      o << "Reference captured and ready for other clips: " << c.name << " r"
        << c.revision << " · " << std::hex << fingerprint(c);
      resultStatus = o.str();
    } else {
      c.name = "Camera at frame " + std::to_string(int(c.time));
      c.revision = s.hasTarget ? s.target.revision + 1 : 1;
      auto fit = solve(s.hero, c, g, selectedMatchMethod(ps));
      if (!fit.solution.valid) {
        status(ps, fit.error);
        return done();
      }
      s.target = c;
      s.hasTarget = true;
      s.solution = fit.solution;
      std::ostringstream o;
      o << "Applied reference " << s.hero.name << " ("
        << (fit.solution.method == MatchMethod::Rbf ? "RBF match"
                                                   : "existing match")
        << "): " << fit.solution.neutralCount
        << " neutral, " << fit.solution.colorCount << " color patches";
      if (!fit.error.empty())
        o << " · " << fit.error;
      resultStatus = o.str();
    }
    EditGroup edit(ps, name == "captureHero" ? "Capture reference"
                                             : "Apply reference to this clip");
    if (!commitState(ps, s)) {
      status(ps, "Could not save the captured match in Resolve; previous "
                 "correction kept");
      return done();
    }
    if (name == "captureHero")
      sessionHero.publish(s.hero);
    referenceStatus(ps, s);
    patchReport(ps);
    status(ps, resultStatus);
    return done();
  }
  if (name == "refit") {
    auto s = state(ps);
    if (s.hasHero && s.hasTarget) {
      if (s.hero.chartModel == 1 && geometry(ps).model == 2) {
        status(ps, "Passport patch layout was corrected. Recapture the reference and target.");
        return done();
      }
      auto fit = solve(s.hero, s.target, geometry(ps), selectedMatchMethod(ps));
      if (fit.solution.valid) {
        EditGroup edit(ps, "Refit camera match");
        s.solution = fit.solution;
        if (!commitState(ps, s)) {
          status(
              ps,
              "Could not save the refit in Resolve; previous correction kept");
          return done();
        }
        status(ps, fit.error.empty() ? "Refit captured samples" : fit.error);
        patchReport(ps);
      } else
        status(ps, fit.error);
    } else
      status(ps, "Capture reference and target before refitting");
    return done();
  }
  if (name == "resetAlignment") {
    Geometry g = geometry(ps);
    double time = 0;
    prop->propGetDouble(args, kOfxPropTime, 0, &time);
    OfxRectD rod{};
    if (fx->clipGetRegionOfDefinition(
            clip(e, kOfxImageEffectSimpleSourceClipName), time, &rod) ==
            kOfxStatOK &&
        rod.x2 > rod.x1 && rod.y2 > rod.y1) {
      double l = rod.x1 + .2 * (rod.x2 - rod.x1),
             r = rod.x1 + .8 * (rod.x2 - rod.x1);
      double b = rod.y1 + .2 * (rod.y2 - rod.y1),
             t = rod.y1 + .8 * (rod.y2 - rod.y1);
      g.corners = {{{l, t}, {r, t}, {r, b}, {l, b}}};
    } else
      g.corners = {{{384, 864}, {1536, 864}, {1536, 216}, {384, 216}}};
    params->paramEditBegin(ps, "Reset chart alignment");
    writeGeometry(ps, g, true);
    params->paramEditEnd(ps);
    status(ps, "Alignment reset; recapture to use new geometry");
    return done();
  }
  if (name == "resetSample") {
    Geometry g = geometry(ps);
    int j = gi(ps, "patchSelector");
    if (j >= 0 && j < int(layout(g.model).size())) {
      g.samples[j] = {0, 0, gd(ps, "sampleInset", 50) / 100.,
                      gd(ps, "sampleInset", 50) / 100.};
      ss(ps, geoPayload, encodeGeometry(g));
      refreshSelected(ps, g, j);
      status(ps, "Sample changed; recapture to use new area");
    }
    return done();
  }
  if (name == "patchSelector") {
    Geometry g = geometry(ps);
    refreshSelected(ps, g, gi(ps, "patchSelector"));
    patchReport(ps);
    return done();
  }
  if (name == "included" || name == "sampleX" || name == "sampleY" ||
      name == "sampleW" || name == "sampleH") {
    Geometry g = geometry(ps);
    int j = gi(ps, "patchSelector");
    if (j >= 0 && j < int(layout(g.model).size())) {
      auto &v = g.samples[j];
      v.dx = gd(ps, "sampleX");
      v.dy = gd(ps, "sampleY");
      v.w = gd(ps, "sampleW", .5);
      v.h = gd(ps, "sampleH", .5);
      if (v.w >= .1 && v.h >= .1 && std::abs(v.dx) + v.w / 2 <= .5 &&
          std::abs(v.dy) + v.h / 2 <= .5) {
        g.included[j] = gi(ps, "included") != 0;
        ss(ps, geoPayload, encodeGeometry(g));
        status(ps, "Patch settings changed; recapture or refit stored samples");
        patchReport(ps);
      } else {
        status(ps, "Sample must stay inside its patch");
        refreshSelected(ps, g, j);
      }
    }
    return done();
  }
  if (name == "sampleInset") {
    Geometry g = geometry(ps);
    double width = gd(ps, "sampleInset", 50) / 100.;
    for (auto &e : g.samples) {
      e.w = width;
      e.h = width;
      e.dx = std::max(-.5 + width / 2, std::min(.5 - width / 2, e.dx));
      e.dy = std::max(-.5 + width / 2, std::min(.5 - width / 2, e.dy));
    }
    ss(ps, geoPayload, encodeGeometry(g));
    refreshSelected(ps, g, gi(ps, "patchSelector"));
    status(ps, "Sample inset changed; recapture to use new areas");
    return done();
  }
  if (name == "chartModel") {
    Geometry g = geometry(ps);
    Geometry defaults;
    auto &p = layout(g.model);
    for (size_t j = 0; j < g.included.size(); j++) {
      g.included[j] = j < p.size() ? p[j].defaultIncluded : false;
      g.samples[j] = {};
    }
    ss(ps, geoPayload, encodeGeometry(g));
    si(ps, "patchSelector", 0);
    refreshSelected(ps, g, 0);
    status(ps, "Chart model changed; align and recapture");
    patchReport(ps);
    return done();
  }
  if (name == "hue" || name == "sat" || name == "exposure" ||
      name == "neutral") {
    patchReport(ps);
    return done();
  }
  if (name == "rotation" || name == "mirror" || name.rfind("corner", 0) == 0 ||
      name == geoPayload) {
    status(ps, "Chart geometry changed; recapture to use new areas");
    return done();
  }
  return done();
}
OfxStatus describe(OfxImageEffectHandle e) {
  auto p = effectProps(e);
  prop->propSetString(p, kOfxPropLabel, 0, "Camera Match");
  prop->propSetString(p, kOfxImageEffectPluginPropGrouping, 0, "LoFi FX");
  prop->propSetString(p, kOfxImageEffectPropSupportedContexts, 0,
                      kOfxImageEffectContextFilter);
  prop->propSetString(p, kOfxImageEffectPropSupportedPixelDepths, 0,
                      kOfxBitDepthFloat);
  prop->propSetInt(p, kOfxImageEffectPropSupportsTiles, 0, 1);
  prop->propSetInt(p, kOfxImageEffectPropSupportsMultiResolution, 0, 1);
#ifdef __APPLE__
  prop->propSetString(p, kOfxImageEffectPropMetalRenderSupported, 0, "true");
#endif
  prop->propSetInt(p, kOfxImageEffectPropSupportsOverlays, 0,
                   interact ? 1 : 0);
  if (interact)
    prop->propSetPointer(
        p, draw && useDrawSuite ? kOfxImageEffectPluginPropOverlayInteractV2
                : kOfxImageEffectPluginPropOverlayInteractV1,
        0, (void *)overlayMain);
  return kOfxStatOK;
}
OfxStatus describeContext(OfxImageEffectHandle e) {
  descriptorParams.clear();
  OfxPropertySetHandle cp = nullptr;
  fx->clipDefine(e, kOfxImageEffectSimpleSourceClipName, &cp);
  prop->propSetString(cp, kOfxImageEffectPropSupportedComponents, 0,
                      kOfxImageComponentRGBA);
  prop->propSetString(cp, kOfxImageEffectPropSupportedComponents, 1,
                      kOfxImageComponentRGB);
  fx->clipDefine(e, kOfxImageEffectOutputClipName, &cp);
  prop->propSetString(cp, kOfxImageEffectPropSupportedComponents, 0,
                      kOfxImageComponentRGBA);
  prop->propSetString(cp, kOfxImageEffectPropSupportedComponents, 1,
                      kOfxImageComponentRGB);
  auto ps = paramSet(e);
  define(ps, kOfxParamTypeGroup, "captureGroup", "Capture");
  prop->propSetInt(desc(ps, "captureGroup"), kOfxParamPropGroupOpen, 0, 1);
  define(ps, kOfxParamTypeString, "heroName", "Save reference as");
  parent(ps, "heroName", "captureGroup");
  prop->propSetString(desc(ps, "heroName"), kOfxParamPropDefault, 0, "");
  define(ps, kOfxParamTypePushButton, "captureHero", "Capture reference");
  parent(ps, "captureHero", "captureGroup");

  define(ps, kOfxParamTypeGroup, "applyGroup", "Apply reference adjustments");
  prop->propSetInt(desc(ps, "applyGroup"), kOfxParamPropGroupOpen, 0, 1);
  define(ps, kOfxParamTypeString, "heroToApply", "Apply reference named");
  parent(ps, "heroToApply", "applyGroup");
  prop->propSetString(desc(ps, "heroToApply"), kOfxParamPropDefault, 0, "");
  choice(ps, "matchMethod", "Match method", {"Existing match", "RBF match"});
  parent(ps, "matchMethod", "applyGroup");
  define(ps, kOfxParamTypePushButton, "analyze",
         "Apply reference to this clip");
  parent(ps, "analyze", "applyGroup");
  define(ps, kOfxParamTypePushButton, "listHeroes",
         "List captured references");
  parent(ps, "listHeroes", "applyGroup");

  define(ps, kOfxParamTypeGroup, "colorChartGroup", "Color chart");
  prop->propSetInt(desc(ps, "colorChartGroup"), kOfxParamPropGroupOpen, 0, 1);
  choice(ps, "chartModel", "Chart model",
         {"Color Checker Video", "Color Checker Passport Video"});
  parent(ps, "chartModel", "colorChartGroup");
  prop->propSetInt(desc(ps, "chartModel"), kOfxParamPropDefault, 0, 1);
  define(ps, kOfxParamTypeBoolean, "showOverlay",
         "Show color chart overlay");
  parent(ps, "showOverlay", "colorChartGroup");
  prop->propSetInt(desc(ps, "showOverlay"), kOfxParamPropDefault, 0, 1);
  choice(ps, "editMode", "Edit mode",
         {"Align chart", "Select patches", "Adjust samples"});
  parent(ps, "editMode", "colorChartGroup");
  choice(ps, "rotation", "Rotate chart",
         {"0 degrees", "90 degrees", "180 degrees", "270 degrees"});
  parent(ps, "rotation", "colorChartGroup");
  define(ps, kOfxParamTypeBoolean, "mirror", "Mirror patch identity");
  parent(ps, "mirror", "colorChartGroup");
  prop->propSetInt(desc(ps, "mirror"), kOfxParamPropDefault, 0, 0);

  define(ps, kOfxParamTypeGroup, "adjustmentsGroup",
         "Reference adjustments");
  prop->propSetInt(desc(ps, "adjustmentsGroup"), kOfxParamPropGroupOpen, 0, 1);
  for (auto pair : {std::pair<const char *, const char *>{"hue", "Hue match"},
                    {"sat", "Saturation match"},
                    {"exposure", "Exposure match"},
                    {"neutral", "Neutral balance match"}}) {
    define(ps, kOfxParamTypeDouble, pair.first, pair.second);
    parent(ps, pair.first, "adjustmentsGroup");
    defaultDouble(ps, pair.first, 100, 0, 100);
  }
  define(ps, kOfxParamTypeDouble, "biasWeight", "RBF weight %");
  parent(ps, "biasWeight", "adjustmentsGroup");
  defaultDouble(ps, "biasWeight", 100, 0, 200);
  prop->propSetInt(desc(ps, "biasWeight"), kOfxParamPropSecret, 0, 1);

  define(ps, kOfxParamTypeBoolean, "bypass", "Bypass");
  prop->propSetInt(desc(ps, "bypass"), kOfxParamPropDefault, 0, 0);
  define(ps, kOfxParamTypeString, "referenceStatus", "Reference status");
  prop->propSetString(
      desc(ps, "referenceStatus"), kOfxParamPropDefault, 0,
      "No reference on this node. Enter Apply reference named, then apply.");
  prop->propSetInt(desc(ps, "referenceStatus"), kOfxParamPropEnabled, 0, 0);
  define(ps, kOfxParamTypeString, "status", "Status");
  prop->propSetString(
      desc(ps, "status"), kOfxParamPropDefault, 0,
      "Name and capture a reference, or enter its name and apply it to this clip");
  prop->propSetInt(desc(ps, "status"), kOfxParamPropEnabled, 0, 0);

  define(ps, kOfxParamTypeGroup, "advanced", "Advanced");
  prop->propSetInt(desc(ps, "advanced"), kOfxParamPropGroupOpen, 0, 0);
  define(ps, kOfxParamTypePushButton, "makeHeroAvailable",
         "Use this reference for other clips");
  parent(ps, "makeHeroAvailable", "advanced");
  define(ps, kOfxParamTypePushButton, "resetAlignment", "Reset alignment");
  parent(ps, "resetAlignment", "advanced");
  const Point c[4] = {{384, 864}, {1536, 864}, {1536, 216}, {384, 216}};
  const char *labels[] = {"Top left", "Top right", "Bottom right",
                          "Bottom left"};
  for (int j = 0; j < 4; j++) {
    auto n = cornerName(j);
    define(ps, kOfxParamTypeDouble2D, n.c_str(), labels[j]);
    auto q = desc(ps, n.c_str());
    prop->propSetString(q, kOfxParamPropDoubleType, 0,
                        kOfxParamDoubleTypeXYAbsolute);
    prop->propSetDouble(q, kOfxParamPropDefault, 0, c[j].x);
    prop->propSetDouble(q, kOfxParamPropDefault, 1, c[j].y);
    prop->propSetInt(q, kOfxParamPropSecret, 0, 1);
  }
  define(ps, kOfxParamTypeString, geoPayload, "Geometry data");
  prop->propSetInt(desc(ps, geoPayload), kOfxParamPropSecret, 0, 1);
  Geometry defaultGeometry;
  defaultGeometry.model = 2;
  for (size_t j = 0; j < defaultGeometry.included.size(); ++j)
    defaultGeometry.included[j] =
        j < layout(2).size() ? layout(2)[j].defaultIncluded : false;
  prop->propSetString(desc(ps, geoPayload), kOfxParamPropDefault, 0,
                      encodeGeometry(defaultGeometry).c_str());
  define(ps, kOfxParamTypeChoice, "patchSelector", "Selected patch");
  parent(ps, "patchSelector", "advanced");
  auto q = desc(ps, "patchSelector");
  for (int j = 0; j < 32; j++) {
    std::string label = "Patch " + std::to_string(j + 1);
    prop->propSetString(q, kOfxParamPropChoiceOption, j, label.c_str());
  }
  define(ps, kOfxParamTypeBoolean, "included", "Include selected patch");
  parent(ps, "included", "advanced");
  prop->propSetInt(desc(ps, "included"), kOfxParamPropDefault, 0, 1);
  for (auto pair :
       {std::pair<const char *, const char *>{"sampleX", "Sample offset X"},
        {"sampleY", "Sample offset Y"},
        {"sampleW", "Sample width"},
        {"sampleH", "Sample height"}}) {
    define(ps, kOfxParamTypeDouble, pair.first, pair.second);
    parent(ps, pair.first, "advanced");
    defaultDouble(ps, pair.first,
                  pair.first[6] == 'W' || pair.first[6] == 'H' ? .5 : 0,
                  pair.first[6] == 'W' || pair.first[6] == 'H' ? .1 : -.45,
                  pair.first[6] == 'W' || pair.first[6] == 'H' ? 1 : .45);
  }
  define(ps, kOfxParamTypeDouble, "sampleInset", "All patch sample size %");
  parent(ps, "sampleInset", "advanced");
  defaultDouble(ps, "sampleInset", 50, 10, 90);
  define(ps, kOfxParamTypePushButton, "resetSample", "Reset selected sample");
  parent(ps, "resetSample", "advanced");
  define(ps, kOfxParamTypePushButton, "refit", "Refit captured samples");
  parent(ps, "refit", "advanced");
  define(ps, kOfxParamTypeString, "inputContract", "Input contract");
  parent(ps, "inputContract", "advanced");
  prop->propSetString(desc(ps, "inputContract"), kOfxParamPropDefault, 0,
                      "DaVinci Wide Gamut / Intermediate; normalize upstream");
  prop->propSetInt(desc(ps, "inputContract"), kOfxParamPropEnabled, 0, 0);
  define(ps, kOfxParamTypeString, "patchReport", "Selected patch report");
  parent(ps, "patchReport", "advanced");
  prop->propSetString(desc(ps, "patchReport"), kOfxParamPropDefault, 0,
                      "No sample yet");
  prop->propSetInt(desc(ps, "patchReport"), kOfxParamPropEnabled, 0, 0);
  define(ps, kOfxParamTypeString, payload, "Captured match data");
  prop->propSetInt(desc(ps, payload), kOfxParamPropSecret, 0, 1);
  prop->propSetString(desc(ps, payload), kOfxParamPropDefault, 0,
                      serialize(Persistent()).c_str());
  descriptorParams.clear();
  return kOfxStatOK;
}
OfxStatus mainEntry(const char *action, const void *handle,
                    OfxPropertySetHandle in, OfxPropertySetHandle out) {
  try {
    auto e = (OfxImageEffectHandle)handle;
    if (!strcmp(action, kOfxActionLoad)) {
      if (!host)
        return kOfxStatFailed;
      prop = (const OfxPropertySuiteV1 *)host->fetchSuite(host->host,
                                                          kOfxPropertySuite, 1);
      fx = (const OfxImageEffectSuiteV1 *)host->fetchSuite(
          host->host, kOfxImageEffectSuite, 1);
      params = (const OfxParameterSuiteV1 *)host->fetchSuite(
          host->host, kOfxParameterSuite, 1);
      interact = (const OfxInteractSuiteV1 *)host->fetchSuite(
          host->host, kOfxInteractSuite, 1);
      draw = (const OfxDrawSuiteV1 *)host->fetchSuite(host->host, kOfxDrawSuite,
                                                      1);
      return prop && fx && params ? kOfxStatOK : kOfxStatErrMissingHostFeature;
    }
    if (!strcmp(action, kOfxActionUnload))
      return kOfxStatOK;
    if (!strcmp(action, kOfxActionDescribe))
      return describe(e);
    if (!strcmp(action, kOfxImageEffectActionDescribeInContext))
      return describeContext(e);
    if (!strcmp(action, kOfxActionCreateInstance)) {
      auto *i = new Instance;
      setInstance(e, i);
      auto ps = paramSet(e);
      updateMatchControls(ps);
      auto savedMatch = state(ps);
      if (savedMatch.hasHero && savedMatch.hero.chartModel == 1)
        status(ps, "Passport patch layout was corrected. Recapture the reference and target.");
      else if (savedMatch.hasTarget &&
          savedMatch.solution.method == MatchMethod::RadialLegacy)
        status(ps, "Saved legacy radial fit. Press Apply reference to this clip to use RBF match.");
      std::string name = trimName(gs(ps, "heroName"));
      if (name.empty()) {
        name = suggestReferenceName(i);
        ss(ps, "heroName", name);
      } else {
        claimReferenceName(i, name);
      }
      return kOfxStatOK;
    }
    if (!strcmp(action, kOfxActionDestroyInstance)) {
      auto *i = instance(e);
      setInstance(e, nullptr);
      if (i)
        claimReferenceName(i, "");
      delete i;
      return kOfxStatOK;
    }
    if (!strcmp(action, kOfxImageEffectActionRender))
      return render(e, in);
    if (!strcmp(action, kOfxActionInstanceChanged))
      return changed(e, in);
    if (!strcmp(action, kOfxImageEffectActionIsIdentity)) {
      auto ps = paramSet(e);
      auto s = state(ps);
      if (!s.solution.valid || gi(ps, "bypass") ||
          (s.solution.method == MatchMethod::Rbf
               ? gd(ps, "biasWeight", 100) == 0
               : ((s.solution.method == MatchMethod::RadialLegacy
                       ? gd(ps, "biasWeight", 100) == 0
                       : gd(ps, "hue") == 0 && gd(ps, "sat") == 0) &&
                  gd(ps, "exposure") == 0 && gd(ps, "neutral") == 0))) {
        prop->propSetString(out, kOfxPropName, 0,
                            kOfxImageEffectSimpleSourceClipName);
        return kOfxStatOK;
      }
      return kOfxStatReplyDefault;
    }
    return kOfxStatReplyDefault;
  } catch (...) {
    return kOfxStatFailed;
  }
}
void setHost(OfxHost *h) { host = h; }
OfxPlugin plugin = {kOfxImageEffectPluginApi, 1, id, 0, 13, setHost, mainEntry};
} // namespace
extern "C" {
OfxExport int OfxGetNumberOfPlugins() { return 1; }
OfxExport OfxPlugin *OfxGetPlugin(int i) { return i == 0 ? &plugin : nullptr; }
}
