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
  for (int model = 0; model < 2; model++)
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
  for (int model = 0; model < 2; model++) {
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
  std::cout << "core checks passed\n";
}
