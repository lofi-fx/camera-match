#import <Metal/Metal.h>
#include "Match.h"
#include "MetalRender.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <chrono>
#include <iostream>
#include <string>

using namespace cm;
int main(int argc, char **argv) {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    assert(device);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    constexpr int width = 256, height = 64;
    constexpr int count = width * height * 4;
    id<MTLBuffer> src = [device newBufferWithLength:count * sizeof(float)
                                          options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst = [device newBufferWithLength:count * sizeof(float)
                                          options:MTLResourceStorageModeShared];
    auto *input = static_cast<float *>(src.contents);
    auto *output = static_cast<float *>(dst.contents);
    Solution solution;
    solution.valid = true;
    solution.stops = .6;
    solution.neutralLog = {.08, -.03, -.05};
    solution.hue = {.03, .015, -.02};
    solution.sat = {.08, -.02, .03};
    Amounts amount{.7, .8, .5, .9, false};
    MetalParams p{};
    for (int j = 0; j < 3; ++j) {
      p.hue[j] = float(solution.hue[j]);
      p.sat[j] = float(solution.sat[j]);
    }
    p.neutral[0] = float(solution.neutralLog.r);
    p.neutral[1] = float(solution.neutralLog.g);
    p.neutral[2] = float(solution.neutralLog.b);
    p.stops = float(solution.stops);
    p.hueAmount = float(amount.hue);
    p.satAmount = float(amount.sat);
    p.exposureAmount = float(amount.exposure);
    p.neutralAmount = float(amount.neutral);
    p.srcW = p.dstW = p.winW = width;
    p.srcH = p.dstH = p.winH = height;
    p.srcRowFloats = p.dstRowFloats = width * 4;
    p.srcComponents = p.dstComponents = 4;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        int i = (y * width + x) * 4;
        input[i] = x % 7 == 0 ? -.08f : .02f + x * .02f;
        input[i + 1] = y % 9 == 0 ? 1.35f : .05f + y * .025f;
        input[i + 2] = x % 11 == 0 ? -.02f : .7f - x * .015f;
        input[i + 3] = .5f;
        output[i] = output[i + 1] = output[i + 2] = output[i + 3] = -5.f;
      }
    assert(renderMetal((__bridge void *)queue, (__bridge void *)src,
                       (__bridge void *)dst, p));
    id<MTLCommandBuffer> fence = [queue commandBuffer];
    [fence commit];
    [fence waitUntilCompleted];
    assert(fence.status == MTLCommandBufferStatusCompleted);
    double maxError = 0;
    for (int i = 0; i < count; i += 4) {
      RGB expected = transform({input[i], input[i + 1], input[i + 2]},
                               solution, amount);
      maxError = std::max({maxError, std::abs(output[i] - expected.r),
                           std::abs(output[i + 1] - expected.g),
                           std::abs(output[i + 2] - expected.b)});
      assert(output[i + 3] == .5f);
    }
    assert(maxError < .003);
    solution.method = MatchMethod::RadialLegacy;
    solution.radialCount = 1;
    solution.radial[0] = {.15, .12, .12, .2, 1};
    auto lut = makeRadialLut(solution);
    p.method = 1;
    p.biasWeight = 1.4f;
    amount.biasWeight = 1.4;
    assert(renderMetal((__bridge void *)queue, (__bridge void *)src,
                       (__bridge void *)dst, p, &lut));
    fence = [queue commandBuffer];
    [fence commit];
    [fence waitUntilCompleted];
    assert(fence.status == MTLCommandBufferStatusCompleted);
    maxError = 0;
    for (int i = 0; i < count; i += 4) {
      RGB expected = transform({input[i], input[i + 1], input[i + 2]},
                               solution, amount, &lut);
      maxError = std::max({maxError, std::abs(output[i] - expected.r),
                           std::abs(output[i + 1] - expected.g),
                           std::abs(output[i + 2] - expected.b)});
    }
    assert(maxError < .003);
    solution = {};
    solution.valid = true;
    solution.method = MatchMethod::Rbf;
    solution.rbfCount = 25;
    solution.rbfSupport = .1;
    for (int j = 0; j < solution.rbfCount; ++j) {
      solution.rbfCenters[j] = {.1 + .2 * (j % 5),
                                 .1 + .2 * (j / 5), .5};
      solution.rbfWeights[j] = {.01 * (j % 3 - 1),
                                 .008 * (j % 4 - 2), .004};
    }
    solution.rbfAffine[1] = {1, 0, 0};
    solution.rbfAffine[2] = {0, 1, 0};
    solution.rbfAffine[3] = {0, 0, 1};
    p.rbfCount = solution.rbfCount;
    p.rbfInvSupportSq = 1.f / float(solution.rbfSupport * solution.rbfSupport);
    for (int j = 0; j < solution.rbfCount; ++j) {
      p.rbfCenter[j][0] = float(solution.rbfCenters[j].r);
      p.rbfCenter[j][1] = float(solution.rbfCenters[j].g);
      p.rbfCenter[j][2] = float(solution.rbfCenters[j].b);
      p.rbfWeight[j][0] = float(solution.rbfWeights[j].r);
      p.rbfWeight[j][1] = float(solution.rbfWeights[j].g);
      p.rbfWeight[j][2] = float(solution.rbfWeights[j].b);
    }
    for (int j = 0; j < 4; ++j) {
      p.rbfAffine[j][0] = float(solution.rbfAffine[j].r);
      p.rbfAffine[j][1] = float(solution.rbfAffine[j].g);
      p.rbfAffine[j][2] = float(solution.rbfAffine[j].b);
    }
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        int i = (y * width + x) * 4;
        input[i] = float(encodeIntermediate(x == 0 ? -.1 :
                          x == width - 1 ? 4. : double(x) / (width - 1)));
        input[i + 1] = float(encodeIntermediate(.5));
        input[i + 2] = float(encodeIntermediate(.5));
      }
    p.method = 2;
    p.biasWeight = 1.2f;
    amount.biasWeight = 1.2;
    assert(renderMetal((__bridge void *)queue, (__bridge void *)src,
                       (__bridge void *)dst, p));
    fence = [queue commandBuffer];
    [fence commit];
    [fence waitUntilCompleted];
    assert(fence.status == MTLCommandBufferStatusCompleted);
    maxError = 0;
    for (int i = 0; i < count; i += 4) {
      RGB expected = transform({input[i], input[i + 1], input[i + 2]},
                               solution, amount);
      maxError = std::max({maxError, std::abs(output[i] - expected.r),
                           std::abs(output[i + 1] - expected.g),
                           std::abs(output[i + 2] - expected.b)});
    }
    assert(maxError < .0005);
    // Dense DI ramps exercise the native model across shadows, highlights,
    // negative channels and values above one. Compare every pixel to doubles.
    solution.rbfSpace = RbfSpace::Intermediate;
    p.rbfSpace = 1;
    for (int i = 0; i < count; i += 4) {
      float v = -.1f + 1.4f * (i / 4) / (width * height - 1);
      input[i] = v;
      input[i + 1] = v * .8f;
      input[i + 2] = v * .6f;
    }
    assert(renderMetal((__bridge void *)queue, (__bridge void *)src,
                       (__bridge void *)dst, p));
    fence = [queue commandBuffer];
    [fence commit];
    [fence waitUntilCompleted];
    assert(fence.status == MTLCommandBufferStatusCompleted);
    maxError = 0;
    for (int i = 0; i < count; i += 4) {
      RGB expected = transform({input[i], input[i + 1], input[i + 2]}, solution, amount);
      maxError = std::max({maxError, std::abs(output[i] - expected.r),
                          std::abs(output[i + 1] - expected.g),
                          std::abs(output[i + 2] - expected.b)});
      for (int c = 0; c < 3; ++c) {
        assert(std::isfinite(output[i + c]));
        if (i) {
          assert(output[i + c] > output[i - 4 + c]);
          assert(output[i + c] - output[i - 4 + c] < .0002f);
        }
      }
      assert(output[i + 3] == .5f);
    }
    assert(maxError < 2e-6);
    p.exactCopy = 1;
    assert(renderMetal((__bridge void *)queue, (__bridge void *)src,
                       (__bridge void *)dst, p));
    fence = [queue commandBuffer];
    [fence commit];
    [fence waitUntilCompleted];
    for (int i = 0; i < count; ++i)
      assert(output[i] == input[i]);
    if (argc > 1 && std::string(argv[1]) == "--benchmark") {
      constexpr int bw = 3840, bh = 2160, frames = 30;
      constexpr size_t bytes = size_t(bw) * bh * 4 * sizeof(float);
      id<MTLBuffer> benchSrc = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
      id<MTLBuffer> benchDst = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
      assert(benchSrc && benchDst);
      float *pixels = static_cast<float *>(benchSrc.contents);
      for (size_t i = 0; i < bytes / sizeof(float); i += 4) {
        float v = float(i / 4 % bw) / bw;
        pixels[i] = v; pixels[i + 1] = v * .8f;
        pixels[i + 2] = v * .6f; pixels[i + 3] = 1;
      }
      p.exactCopy = 0;
      p.srcW = p.dstW = p.winW = bw;
      p.srcH = p.dstH = p.winH = bh;
      p.srcRowFloats = p.dstRowFloats = bw * 4;
      auto finish = [&]() {
        id<MTLCommandBuffer> done = [queue commandBuffer];
        [done commit]; [done waitUntilCompleted];
        assert(done.status == MTLCommandBufferStatusCompleted);
      };
      assert(renderMetal((__bridge void *)queue, (__bridge void *)benchSrc,
                         (__bridge void *)benchDst, p));
      finish();
      auto start = std::chrono::steady_clock::now();
      for (int frame = 0; frame < frames; ++frame)
        assert(renderMetal((__bridge void *)queue, (__bridge void *)benchSrc,
                           (__bridge void *)benchDst, p));
      finish();
      double ms = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start).count() / frames;
      std::cout << [device.name UTF8String] << ": 4K RGBA float, "
                << p.rbfCount << " full Gaussian centers, " << ms
                << " ms/frame, " << 1000 / ms << " frames/s (isolated Metal batch)\n";
      [benchSrc release]; [benchDst release];
    }
  }
}
