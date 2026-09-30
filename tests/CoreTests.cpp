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
#include <thread>
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
  for (int j = 0; j < 16; ++j)
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
  for (int j = 0; j < 14; ++j)
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
    hero.geometry.model = target.geometry.model = model;
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
    for (int j = 0; j < 14; ++j)
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
    // Resolve calls render on worker threads with much smaller stacks than main.
    std::thread worker([&] {
      auto lookup = makeRbfLut(rbf.solution);
      assert(lookup && std::isfinite(lookup->values[0]));
    });
    worker.join();
    auto rbfLut = makeRbfLut(rbf.solution);
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
    RGB lookup = transform(source, rbf.solution, rbfAmount, nullptr, rbfLut.get());
    assert(std::abs(fitted.r - lookup.r) + std::abs(fitted.g - lookup.g) +
               std::abs(fitted.b - lookup.b) < .06);
    double worstLookupError = 0;
    for (int ri = 0; ri <= 6; ++ri)
      for (int gi = 0; gi <= 6; ++gi)
        for (int bi = 0; bi <= 6; ++bi) {
          RGB probe{.05 + .15 * ri, .05 + .15 * gi, .05 + .15 * bi};
          RGB direct = transform(probe, rbf.solution, rbfAmount);
          RGB gridded = transform(probe, rbf.solution, rbfAmount, nullptr,
                                  rbfLut.get());
          worstLookupError = std::max(
              {worstLookupError, std::abs(direct.r - gridded.r),
               std::abs(direct.g - gridded.g),
               std::abs(direct.b - gridded.b)});
        }
    assert(worstLookupError < .01);
    RGB black = transform({0, 0, 0}, rbf.solution, rbfAmount);
    assert(std::abs(black.r) + std::abs(black.g) + std::abs(black.b) < .03);
    rbfAmount.biasWeight = 0;
    RGB noRbf = transform(source, rbf.solution, rbfAmount, nullptr, rbfLut.get());
    assert(noRbf.r == source.r && noRbf.g == source.g && noRbf.b == source.b);
    savedRadial.solution = rbf.solution;
    assert(deserialize(serialize(savedRadial), reopened));
    assert(reopened.solution.method == MatchMethod::Rbf);
    assert(reopened.solution.rbfCount == rbf.solution.rbfCount);
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
  // Golden values from Color Workspace's Python fit_rbf_model with the
  // DWG/DI preset's support, regularization, and black anchor.
  const std::array<RGB, 8> pythonSource = {{{.15, .20, .22},
                                             {.30, .25, .20},
                                             {.45, .38, .28},
                                             {.62, .40, .30},
                                             {.22, .48, .32},
                                             {.38, .55, .46},
                                             {.60, .60, .56},
                                             {.72, .68, .64}}};
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
    pythonTarget.patch[j].rgb = decode(pythonSource[j]);
    pythonHero.patch[j].rgb = decode(pythonSource[j] + pythonDelta[j]);
    pythonTarget.patch[j].valid = pythonHero.patch[j].valid = 100;
  }
  auto pythonFit = solve(pythonHero, pythonTarget, pythonGeometry,
                         MatchMethod::Rbf);
  assert(pythonFit.solution.valid && pythonFit.solution.rbfCount == 9);
  const std::array<RGB, 4> probes = {{{.2, .3, .4},
                                       {.45, .42, .35},
                                       {.65, .57, .5},
                                       {0, 0, 0}}};
  const std::array<RGB, 4> expected = {{{.209813374562, .278988735824,
                                          .401763688336},
                                         {.469776315979, .425905522436,
                                          .359464214851},
                                         {.669459286823, .574871396064,
                                          .508043512551},
                                         {.001272684036, -.000267901954,
                                          -.000385090074}}};
  for (size_t j = 0; j < probes.size(); ++j) {
    RGB actual = transform(probes[j], pythonFit.solution,
                           Amounts{0, 0, 0, 0, false, 1});
    assert(std::abs(actual.r - expected[j].r) < 1e-5);
    assert(std::abs(actual.g - expected[j].g) < 1e-5);
    assert(std::abs(actual.b - expected[j].b) < 1e-5);
  }
  std::cout << "core checks passed\n";
}
