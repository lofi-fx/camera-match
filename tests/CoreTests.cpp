#include "Chart.h"
#include "Color.h"
#include "HeroTransfer.h"
#include "ImageView.h"
#include "Match.h"
#include "PatchSampler.h"
#include "State.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace cm;
int main() {
  Geometry g;
  g.corners = {{{100, 500}, {700, 470}, {650, 80}, {130, 100}}};
  auto h = makeHomography(g);
  assert(h.valid);
  for (auto p :
       std::array<Point, 5>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {.3, .7}}}) {
    auto q = h.unproject(h.project(p));
    assert(std::hypot(q.x - p.x, q.y - p.y) < 1e-9);
  }
  g.corners[2] = {0, 800};
  assert(!makeHomography(g).valid);
  assert(layout().size() == 30);
  assert(layout(1).size() == 24);
  assert(layout(2).size() == 24);
  assert(layout(2)[0].box.y1 == layout(2)[5].box.y1);
  assert(layout(2)[6].box.y1 > layout(2)[0].box.y1);
  assert(layout(2)[5].box.x1 > layout(2)[4].box.x1);
  for (const auto &patch : layout(2))
    assert(patch.defaultIncluded);
  for (double x : {-.2, -.01, 0., .001, .05, .5, 1., 4.})
    assert(std::abs(decodeIntermediate(encodeIntermediate(x)) - x) < 1e-10);
  RGB x{.3, .4, .2};
  RGB y = fromOklab(toOklab(x));
  assert(std::abs(x.r - y.r) < 1e-6 && std::abs(x.g - y.g) < 1e-6 &&
         std::abs(x.b - y.b) < 1e-6);
  HeroTransfer transfer;
  Capture first;
  first.name = "Hero A";
  first.revision = 7;
  first.patch[0].rgb = {.2, .3, .4};
  transfer.publish(first);
  first.name = "Changed locally";
  auto shared = transfer.find("Hero A");
  assert(shared && shared->name == "Hero A");
  Persistent received;
  received.hasHero = true;
  received.hero = *shared;
  Capture second;
  second.name = "Hero B";
  transfer.publish(second);
  assert(received.hero.name == "Hero A");
  assert(transfer.find("Hero A")->revision == 7);
  assert(transfer.find("Hero B")->name == "Hero B");
  assert(!transfer.find("Hero C"));
  assert((transfer.names() == std::vector<std::string>{"Hero A", "Hero B"}));
  Capture alias = received.hero;
  alias.name = "Scene 1";
  transfer.publish(alias);
  assert(transfer.find("Scene 1")->revision == 7);
  assert(received.hero.name == "Hero A");
  Persistent restored;
  assert(deserialize(serialize(received), restored));
  assert(restored.hero.name == "Hero A" && restored.hero.revision == 7);
  std::string oldBody = serialize(received);
  oldBody.resize(oldBody.find_last_of(' '));
  for (int j = 0; j < 17; ++j)
    oldBody.resize(oldBody.find_last_of(' '));
  oldBody.replace(0, 3, "CM2");
  uint64_t oldHash = 14695981039346656037ull;
  for (unsigned char c : oldBody) {
    oldHash ^= c;
    oldHash *= 1099511628211ull;
  }
  Persistent legacy;
  assert(deserialize(oldBody + " " + std::to_string(oldHash), legacy));
  assert(legacy.solution.method == MatchMethod::Harmonic);
  std::string cm3Body = serialize(received);
  cm3Body.resize(cm3Body.find_last_of(' '));
  for (int j = 0; j < 15; ++j)
    cm3Body.resize(cm3Body.find_last_of(' '));
  cm3Body.replace(0, 3, "CM3");
  uint64_t cm3Hash = 14695981039346656037ull;
  for (unsigned char c : cm3Body) {
    cm3Hash ^= c;
    cm3Hash *= 1099511628211ull;
  }
  Persistent prior;
  assert(deserialize(cm3Body + " " + std::to_string(cm3Hash), prior));
  assert(prior.solution.method == MatchMethod::Harmonic);
  Persistent p;
  p.hasHero = true;
  p.hero.name = "Test hero";
  p.hero.patch[0].rgb = x;
  auto encoded = serialize(p);
  Persistent parsed;
  assert(deserialize(encoded, parsed));
  assert(parsed.hero.name == p.hero.name);
  encoded[15] = 'x';
  assert(!deserialize(encoded, parsed));
  Solution s;
  s.valid = true;
  s.hue = {.1, .02, -.03};
  s.sat = {.2, .01, .02};
  s.neutralLog = {.1, -.05, -.05};
  s.stops = 1;
  RGB input = encode(x);
  Amounts a;
  a.exposure = 0;
  auto output = decode(transform(input, s, a));
  assert(std::abs(luminance(output) - luminance(x)) < 1e-6);
  a = {0, 0, 0, 0, false};
  auto identity = transform(input, s, a);
  assert(identity.r == input.r && identity.g == input.g &&
         identity.b == input.b);
  float rows[2][16]{};
  FloatImageView view{&rows[1][0], 10, 20, 12, 22, -int(sizeof(rows[0])), 4};
  view.at(10, 20)[0] = 1;
  view.at(11, 21)[2] = 2;
  assert(rows[1][0] == 1 && rows[0][6] == 2);
  assert(view.at(9, 20) == nullptr && view.at(12, 20) == nullptr &&
         view.at(10, 22) == nullptr);
  for (int model = 0; model < 3; model++)
    for (double pixelAspect : {1.0, 1.5}) {
      Geometry chart;
      chart.model = model;
      chart.corners = {{{100, 700}, {1100, 700}, {1100, 100}, {100, 100}}};
      auto map = makeHomography(chart);
      constexpr int x1 = 20, y1 = 10, width = 600, height = 400,
                    pitch = width * 4 + 8;
      std::vector<float> storage(pitch * height, 0);
      FloatImageView pixels{storage.data() + (height - 1) * pitch,
                            x1,
                            y1,
                            x1 + width,
                            y1 + height,
                            -pitch * int(sizeof(float)),
                            4};
      std::vector<RGB> values;
      for (size_t j = 0; j < layout(model).size(); j++)
        values.push_back({.02 + .003 * j, .04 + .002 * j, .06 + .001 * j});
      for (int y = y1; y < y1 + height; y++)
        for (int x = x1; x < x1 + width; x++) {
          int j =
              patchAt(chart, map, {(x + .5) * 2 * pixelAspect, (y + .5) * 2});
          float *p = pixels.at(x, y);
          if (j >= 0) {
            auto rgb = encode(values[j]) * .8;
            p[0] = rgb.r;
            p[1] = rgb.g;
            p[2] = rgb.b;
            p[3] = .8;
          } else
            p[3] = 0;
        }
      SamplingImage img{pixels, pixelAspect, true};
      for (size_t j = 0; j < layout(model).size(); j++) {
        auto obs = samplePatch(img, chart, map, layout(model)[j],
                               chart.samples[j], {.5, .5});
        assert(obs.valid >= 16);
        assert(std::abs(obs.rgb.r - values[j].r) < 1e-6);
        assert(std::abs(obs.rgb.g - values[j].g) < 1e-6);
        assert(std::abs(obs.rgb.b - values[j].b) < 1e-6);
      }
    }
  for (int model = 0; model < 3; model++) {
    Geometry geo;
    geo.model = model;
    for (size_t j = 0; j < layout(model).size(); j++)
      geo.included[j] = layout(model)[j].defaultIncluded;
    Capture hero, target;
    hero.chartModel = target.chartModel = model;
    const RGB colors[6] = {{.45, .08, .04}, {.35, .30, .04}, {.04, .40, .07},
                           {.03, .30, .4},  {.25, .05, .4},  {.4, .04, .20}};
    int ci = 0;
    for (size_t j = 0; j < layout(model).size(); j++) {
      const auto &patch = layout(model)[j];
      RGB v;
      if (patch.role == Role::Neutral)
        v = {.06 + .025 * double(j % 6), .06 + .025 * double(j % 6),
             .06 + .025 * double(j % 6)};
      else
        v = colors[ci++ % 6];
      target.patch[j].rgb = v;
      hero.patch[j].rgb = v * 2;
      target.patch[j].valid = hero.patch[j].valid = 100;
    }
    auto fit = solve(hero, target, geo);
    assert(fit.solution.valid);
    assert(std::abs(fit.solution.stops - 1) < 1e-8);
    Amounts exp{0, 0, 1, 0, false};
    RGB doubled = decode(transform(encode(colors[0]), fit.solution, exp));
    assert(std::abs(doubled.r - 2 * colors[0].r) < 1e-6);
    exp.exposure = .5;
    RGB half = decode(transform(encode(colors[0]), fit.solution, exp));
    assert(std::abs(half.r - std::sqrt(2) * colors[0].r) < 1e-6);
    size_t chosen = 0;
    while (chosen < layout(model).size() &&
           layout(model)[chosen].role != Role::Chromatic)
      ++chosen;
    assert(chosen < layout(model).size());
    Lab shifted = toOklab(hero.patch[chosen].rgb);
    double angle = std::atan2(shifted.b, shifted.a) + .18;
    double chroma = std::hypot(shifted.a, shifted.b);
    hero.patch[chosen].rgb = fromOklab(
        {shifted.L, chroma * std::cos(angle), chroma * std::sin(angle)});
    auto radial = solve(hero, target, geo, MatchMethod::RadialLegacy);
    assert(radial.solution.valid && radial.solution.radialCount >= 4);
    auto harmonic = solve(hero, target, geo, MatchMethod::Harmonic);
    assert(harmonic.solution.valid);
    auto lut = makeRadialLut(radial.solution);
    Amounts chromatic{1, 1, 0, 0, false, 1};
    auto direct = transform(encode(target.patch[chosen].rgb),
                            radial.solution, chromatic);
    auto cached = transform(encode(target.patch[chosen].rgb),
                            radial.solution, chromatic, &lut);
    auto broad = transform(encode(target.patch[chosen].rgb),
                           harmonic.solution, chromatic);
    assert(std::abs(broad.r - cached.r) + std::abs(broad.g - cached.g) +
               std::abs(broad.b - cached.b) > 1e-4);
    assert(std::abs(direct.r - cached.r) < .003);
    assert(std::abs(direct.g - cached.g) < .003);
    assert(std::abs(direct.b - cached.b) < .003);
    chromatic.biasWeight = 0;
    auto zero = transform(encode(target.patch[chosen].rgb),
                          radial.solution, chromatic, &lut);
    RGB original = encode(target.patch[chosen].rgb);
    assert(std::abs(zero.r - original.r) < 1e-6);
    Persistent savedRadial;
    savedRadial.hasHero = savedRadial.hasTarget = true;
    hero.geometry = target.geometry = geo;
    for (auto &patch : hero.patch)
      patch.candidate = patch.valid;
    for (auto &patch : target.patch)
      patch.candidate = patch.valid;
    savedRadial.hero = hero;
    savedRadial.target = target;
    savedRadial.solution = radial.solution;
    Persistent reopened;
    assert(deserialize(serialize(savedRadial), reopened));
    assert(reopened.solution.method == MatchMethod::RadialLegacy);
    assert(reopened.solution.radialCount == radial.solution.radialCount);
    std::string legacyRadial = serialize(savedRadial);
    legacyRadial.resize(legacyRadial.find_last_of(' '));
    for (int j = 0; j < 15; ++j)
      legacyRadial.resize(legacyRadial.find_last_of(' '));
    legacyRadial.replace(0, 3, "CM3");
    uint64_t checksum = 14695981039346656037ull;
    for (unsigned char c : legacyRadial) {
      checksum ^= c;
      checksum *= 1099511628211ull;
    }
    assert(deserialize(legacyRadial + " " + std::to_string(checksum), reopened));
    assert(reopened.solution.method == MatchMethod::RadialLegacy);
    auto rbf = solve(hero, target, geo, MatchMethod::Rbf);
    assert(rbf.solution.valid && rbf.solution.rbfCount >= 7);
    RGB source = encode(target.patch[chosen].rgb);
    RGB wanted = encode(hero.patch[chosen].rgb);
    Amounts rbfAmount{0, 0, 0, 0, false, 1};
    RGB fitted = transform(source, rbf.solution, rbfAmount);
    double fittedError = std::abs(fitted.r - wanted.r) +
                         std::abs(fitted.g - wanted.g) +
                         std::abs(fitted.b - wanted.b);
    double originalError = std::abs(source.r - wanted.r) +
                           std::abs(source.g - wanted.g) +
                           std::abs(source.b - wanted.b);
    assert(fittedError < originalError);
    size_t neutralPatch = 0;
    while (neutralPatch < layout(model).size() &&
           (!geo.included[neutralPatch] ||
            layout(model)[neutralPatch].role != Role::Neutral))
      ++neutralPatch;
    assert(neutralPatch < layout(model).size());
    RGB neutralSource = encode(target.patch[neutralPatch].rgb);
    RGB neutralWanted = encode(hero.patch[neutralPatch].rgb);
    RGB neutralFitted = transform(neutralSource, rbf.solution, rbfAmount);
    double neutralError = std::abs(neutralFitted.r - neutralWanted.r) +
                          std::abs(neutralFitted.g - neutralWanted.g) +
                          std::abs(neutralFitted.b - neutralWanted.b);
    double neutralBefore = std::abs(neutralSource.r - neutralWanted.r) +
                           std::abs(neutralSource.g - neutralWanted.g) +
                           std::abs(neutralSource.b - neutralWanted.b);
    assert(neutralError < neutralBefore);
    RGB black = transform({0, 0, 0}, rbf.solution, rbfAmount);
    assert(std::abs(black.r) + std::abs(black.g) + std::abs(black.b) < .03);
    rbfAmount.biasWeight = 0;
    RGB noRbf = transform(source, rbf.solution, rbfAmount);
    assert(noRbf.r == source.r && noRbf.g == source.g && noRbf.b == source.b);
    savedRadial.solution = rbf.solution;
    assert(deserialize(serialize(savedRadial), reopened));
    assert(reopened.solution.method == MatchMethod::Rbf);
    assert(reopened.solution.rbfCount == rbf.solution.rbfCount);
    Persistent oldRbf = savedRadial;
    oldRbf.solution.rbfCenters[0] = {9, 9, 9};
    std::string oldRbfBody = serialize(oldRbf);
    oldRbfBody.resize(oldRbfBody.find_last_of(' '));
    oldRbfBody.resize(oldRbfBody.find_last_of(' ')); // CM6 space tag
    oldRbfBody.replace(0, 3, "CM4");
    uint64_t oldRbfHash = 14695981039346656037ull;
    for (unsigned char c : oldRbfBody) {
      oldRbfHash ^= c;
      oldRbfHash *= 1099511628211ull;
    }
    assert(deserialize(oldRbfBody + " " + std::to_string(oldRbfHash), reopened));
    assert(reopened.solution.valid);
    assert(std::abs(reopened.solution.rbfCenters[0].r -
                    rbf.solution.rbfCenters[0].r) < 1e-10);
  }
  for (int rot = 0; rot < 4; rot++)
    for (bool mirror : {false, true}) {
      Point q{.23, .71};
      Point r = unorient(orient(q, rot, mirror), rot, mirror);
      assert(std::hypot(q.x - r.x, q.y - r.y) < 1e-12);
    }
  for (double hue : {0., .35, 1.})
    for (double sat : {0., .6, 1.}) {
      Amounts noEx{hue, sat, 0, 0, false}, fullEx{hue, sat, 1, 0, false};
      RGB a = decode(transform(input, s, noEx)),
          b = decode(transform(input, s, fullEx));
      assert(std::abs(b.r - 2 * a.r) < 1e-6);
      assert(std::abs(b.g - 2 * a.g) < 1e-6);
      assert(std::abs(b.b - 2 * a.b) < 1e-6);
    }
  for (int hue = 0; hue < 2; hue++)
    for (int sat = 0; sat < 2; sat++)
      for (int ex = 0; ex < 2; ex++) {
        Amounts controls{double(hue), double(sat), double(ex), 0, false};
        RGB z = transform(input, s, controls);
        assert(finite(z));
        if (!ex)
          assert(std::abs(luminance(decode(z)) - luminance(x)) < 1e-6);
      }
  for (RGB v : {RGB{-0.1, .02, .01}, RGB{0, 0, 0}, RGB{3, 2, 5}}) {
    auto z = transform(encode(v), s, Amounts{});
    assert(finite(z));
  }
  Solution local;
  local.valid = true;
  local.method = MatchMethod::RadialLegacy;
  local.radialCount = 1;
  local.radial[0] = {.2, .0833333333, .2, 0, 1};
  auto localLut = makeRadialLut(local);
  RGB near = encode(fromOklab({.6, .12, .05}));
  RGB far = encode(fromOklab({.6, -.12, -.05}));
  Amounts localAmount{1, 0, 0, 0, false, 1};
  auto hueShift = [](RGB before, RGB after) {
    auto a = toOklab(decode(before)), b = toOklab(decode(after));
    return std::abs(std::remainder(std::atan2(b.b, b.a) -
                                   std::atan2(a.b, a.a), 2 * M_PI));
  };
  assert(hueShift(near, transform(near, local, localAmount, &localLut)) >
         hueShift(far, transform(far, local, localAmount, &localLut)) + .05);
  localAmount.biasWeight = 0;
  auto unchanged = transform(near, local, localAmount, &localLut);
  assert(unchanged.r == near.r && unchanged.g == near.g &&
         unchanged.b == near.b);
  // Golden values from Color Workspace's DWG/DI preset (encode linear
  // captures before fit_rbf_model and evaluate encoded probes), with support
  // 0.1, regularization 0.2, and a pinned black anchor.
  const std::array<RGB, 8> pythonSource = {{{.06, .09, .11},
                                             {.13, .10, .08},
                                             {.25, .20, .12},
                                             {.41, .21, .16},
                                             {.10, .30, .17},
                                             {.19, .38, .27},
                                             {.45, .45, .41},
                                             {.68, .62, .57}}};
  const std::array<RGB, 8> pythonDelta = {{{.01, -.005, .005},
                                            {.02, 0, -.01},
                                            {.03, .01, .015},
                                            {.01, .02, 0},
                                            {-.01, .015, .02},
                                            {.005, .005, -.005},
                                            {.02, -.005, .01},
                                            {.01, .005, .015}}};
  Geometry pythonGeometry;
  Capture pythonHero, pythonTarget;
  for (int j = 0; j < 8; ++j) {
    pythonTarget.patch[j].rgb = pythonSource[j];
    pythonHero.patch[j].rgb = pythonSource[j] + pythonDelta[j];
    pythonTarget.patch[j].valid = pythonHero.patch[j].valid = 100;
  }
  auto pythonFit = solve(pythonHero, pythonTarget, pythonGeometry,
                         MatchMethod::Rbf);
  assert(pythonFit.solution.valid && pythonFit.solution.rbfCount == 9);
  const std::array<RGB, 5> probes = {{{.1, .15, .2},
                                      {.30, .27, .2},
                                      {.55, .48, .4},
                                      {0, 0, 0},
                                      {4, 3.5, 3}}};
  const std::array<RGB, 5> expected = {{{0.287413036306644, 0.311908227295225, 0.349156411206737},
      {0.393379995780695, 0.382354079745692, 0.349144916697527},
      {0.453843437691407, 0.438543260789005, 0.420159325918301},
      {0.000726976691207, -0.000095802241122, -0.000140693459122},
      {0.666802519901306, 0.648190970061679, 0.629744778835783}}};
  for (size_t j = 0; j < probes.size(); ++j) {
    RGB actual = transform(encode(probes[j]), pythonFit.solution,
                           Amounts{0, 0, 0, 0, false, 1});
    assert(std::abs(actual.r - expected[j].r) < 1e-11);
    assert(std::abs(actual.g - expected[j].g) < 1e-11);
    assert(std::abs(actual.b - expected[j].b) < 1e-11);
  }
  assert(pythonFit.solution.rbfSpace == RbfSpace::Intermediate);
  // An identity fit must preserve fine steps, including negatives and HDR.
  auto identityFit = solve(pythonTarget, pythonTarget, pythonGeometry, MatchMethod::Rbf);
  assert(identityFit.solution.valid);
  Amounts full{0, 0, 0, 0, false, 1};
  RGB previous{};
  for (int j = 0; j <= 65536; ++j) {
    double v = -.1 + 1.4 * j / 65536.;
    RGB input{v, v * .8, v * .6};
    RGB same = transform(input, identityFit.solution, full);
    assert(std::abs(same.r - input.r) < 1e-11);
    assert(std::abs(same.g - input.g) < 1e-11);
    assert(std::abs(same.b - input.b) < 1e-11);
    RGB corrected = transform(input, pythonFit.solution, full);
    assert(finite(corrected));
    if (j) {
      // This smooth reference fit has no plateaus, reversals, or steps.
      assert(corrected.r > previous.r && corrected.g > previous.g && corrected.b > previous.b);
      assert(std::abs(corrected.r - previous.r) < 1e-4);
      assert(std::abs(corrected.g - previous.g) < 1e-4);
      assert(std::abs(corrected.b - previous.b) < 1e-4);
    }
    previous = corrected;
  }
  // A legacy CM5 solution retains linear interpretation and output on reload.
  Persistent legacyLinear;
  legacyLinear.hasHero = legacyLinear.hasTarget = true;
  legacyLinear.solution = pythonFit.solution;
  legacyLinear.solution.rbfSpace = RbfSpace::Linear;
  std::string cm5 = serialize(legacyLinear);
  cm5.resize(cm5.find_last_of(' ')); // checksum
  cm5.resize(cm5.find_last_of(' ')); // CM6 space
  cm5.replace(0, 3, "CM5");
  uint64_t cm5Hash = 14695981039346656037ull;
  for (unsigned char c : cm5) { cm5Hash ^= c; cm5Hash *= 1099511628211ull; }
  Persistent reopenedLinear;
  assert(deserialize(cm5 + " " + std::to_string(cm5Hash), reopenedLinear));
  assert(reopenedLinear.solution.rbfSpace == RbfSpace::Linear);
  RGB before = transform({.3, .4, .5}, legacyLinear.solution, full);
  RGB after = transform({.3, .4, .5}, reopenedLinear.solution, full);
  assert(before.r == after.r && before.g == after.g && before.b == after.b);
  assert(deserialize(serialize(reopenedLinear), reopenedLinear));
  assert(reopenedLinear.solution.rbfSpace == RbfSpace::Linear);
  // Real capture regression: a rotated sampling grid produced a smooth RBF
  // with a reversal in shadow brightness. Do not accept it as a usable fit.
  const double foldingPairs[24][6] = {
      {0.17873856517942133, 0.31263656867094869, 0.27036743513644407, 0.044786461880246799, 0.081001075062351074, 0.071657269209530042},
      {0.061336978636025281, 0.079259682219188027, 0.10524329565683267, 0.042062857937101586, 0.045378370994387553, 0.046799122546891926},
      {0.28055154789088727, 0.29241780342555534, 0.34512629776890053, 0.069759991830977058, 0.06968336835485614, 0.068067151220677677},
      {0.32529895407435355, 0.40401669303864263, 0.53604373301085606, 0.075863520107627522, 0.091385912122136898, 0.10003564102349391},
      {0.30659018503762336, 0.37868062655068158, 0.50174756656817765, 0.024060490091250251, 0.031269924708142417, 0.03512072275023282},
      {0.51045289678731065, 0.63211521314283403, 0.81144749687075335, 0.12318624087677471, 0.1454600918359138, 0.15908129575137031},
      {0.15099895065500932, 0.23035749210506279, 0.60074342543626613, 0.039968551663940595, 0.054415837177108038, 0.10991924233778692},
      {0.056535128002789252, 0.071027489152071313, 0.10347444306002906, 0.048469358068424749, 0.043618581665737587, 0.039063823453003188},
      {0.29245141347910647, 0.26278117742776891, 0.26440686027153104, 0.075055085427254198, 0.067223244119643685, 0.055447344764301638},
      {0.23971494684965111, 0.29833490327757972, 0.38993324046624522, 0.066255663033878115, 0.078903709231706878, 0.084114344305849381},
      {0.18363761502107248, 0.23108456078797648, 0.30544476774687834, 0.035843752699093949, 0.042989551224681344, 0.046931453415131552},
      {0.77815363046962038, 0.97287422480001573, 1.2689153071804322, 0.19586579587150382, 0.23027524142083533, 0.24714779259071532},
      {0.35563305205972828, 0.27857074443979035, 0.67743696714772439, 0.098641214809969083, 0.069003549595656605, 0.12730920760186351},
      {0.070488810304188451, 0.081315694608368391, 0.11757238003919457, 0.055427758550658693, 0.05238600114581262, 0.049127468627310815},
      {0.38043607013264119, 0.36928923181171636, 0.4013882396441858, 0.095899413927432953, 0.089950582418387867, 0.080427546327615354},
      {0.16665273001264486, 0.20642025724268781, 0.27450326619988374, 0.041597272837942977, 0.051007551537606666, 0.055014560892440445},
      {0.14130005092244638, 0.17856015874345615, 0.23802614999186322, 0.024484324413244871, 0.030798247579825966, 0.033199073577673391},
      {0.042894643062185843, 0.055095060352846774, 0.070868831428144527, 0.0095615440450169484, 0.011731592004765234, 0.013975641310491579},
      {0.2454375318119224, 0.22129734058093278, 0.18708280577689304, 0.064375261589292979, 0.058548857333217456, 0.040654728586742954},
      {0.056649212463001378, 0.067270610567787673, 0.088221793139389756, 0.017246792535203261, 0.018908047018615034, 0.02049879763187205},
      {0.10049833908436479, 0.097288157773137679, 0.10859550223953758, 0.024005585222757465, 0.023731994115911276, 0.022195866938460523},
      {0.058805551387202108, 0.073876597034617103, 0.098594666917633869, 0.014567379990436937, 0.018542234650855218, 0.020984424633198159},
      {0.055863519132085968, 0.071355386025951859, 0.096259326550634572, 0.014011486884307917, 0.018433969104775814, 0.02013320687810672},
      {0.045828232334867769, 0.056509865342141384, 0.077375225473779632, 0.011990377435616234, 0.015992667179635458, 0.017306995632010276},
  };
  Geometry foldingGeometry;
  foldingGeometry.model = 2;
  foldingGeometry.included.fill(true);
  Capture foldingHero, foldingTarget;
  foldingHero.chartModel = foldingTarget.chartModel = 2;
  for (int j = 0; j < 24; ++j) {
    const auto &v = foldingPairs[j];
    foldingTarget.patch[j].rgb = {v[0], v[1], v[2]};
    foldingHero.patch[j].rgb = {v[3], v[4], v[5]};
    foldingHero.patch[j].valid = foldingTarget.patch[j].valid = 100;
  }
  auto folding = solve(foldingHero, foldingTarget, foldingGeometry, MatchMethod::Rbf);
  assert(!folding.solution.valid);
  assert(folding.error.find("brightness reverses") != std::string::npos);
  std::cout << "core checks passed\n";
}
