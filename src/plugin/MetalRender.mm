#import <Metal/Metal.h>
#include "MetalRender.h"
#include <mutex>
#include <vector>

namespace cm {
namespace {
constexpr const char *shader = R"METAL(
#include <metal_stdlib>
using namespace metal;
struct Params {
  float4 hue, sat, neutral;
  float stops, hueAmount, satAmount, exposureAmount;
  float neutralAmount;
  float biasWeight;
  int method;
  int srcX, srcY, srcW, srcH;
  int dstX, dstY, dstW, dstH;
  int winX, winY, winW, winH;
  int srcRowFloats, dstRowFloats;
  int srcComponents, dstComponents;
  int srcPremult, dstPremult;
  int exactCopy;
};
float3 decodeDI(float3 x) {
  return select(exp2(x / .07329248f - 7.f) - .0075f,
                x / 10.44426855f, x <= .02740668f);
}
float3 encodeDI(float3 x) {
  return select((log2(x + .0075f) + 7.f) * .07329248f,
                x * 10.44426855f, x <= .00262409f);
}
float luma(float3 x) { return dot(x, float3(.27411851f, .87363190f, -.1477249265f)); }
float3 toLab(float3 x) {
  float3 xyz = float3(dot(x, float3(.70062239f,.14877482f,.10105872f)),
                      dot(x, float3(.27411851f,.87363190f,-.14775041f)),
                      dot(x, float3(-.09896291f,-.13789533f,1.32591599f)));
  float3 l = float3(dot(xyz,float3(.8189330101f,.3618667424f,-.1288597137f)),
                    dot(xyz,float3(.0329845436f,.9293118715f,.0361456387f)),
                    dot(xyz,float3(.0482003018f,.2643662691f,.6338517070f)));
  l = sign(l) * pow(abs(l), float3(1.f/3.f));
  return float3(dot(l,float3(.2104542553f,.7936177850f,-.0040720468f)),
                dot(l,float3(1.9779984951f,-2.4285922050f,.4505937099f)),
                dot(l,float3(.0259040371f,.7827717662f,-.8086757660f)));
}
float3 fromLab(float3 v) {
  float3 l = float3(dot(v,float3(1.f,.396337777376175f,.215803757309914f)),
                    dot(v,float3(1.f,-.105561345815659f,-.063854172825813f)),
                    dot(v,float3(1.f,-.089484177529812f,-1.291485548019410f)));
  l = l*l*l;
  float3 xyz = float3(dot(l,float3(1.227013851103521f,-.557799980651822f,.281256148966468f)),
                      dot(l,float3(-.040580178423281f,1.112256869616830f,-.071676678665601f)),
                      dot(l,float3(-.076381284505707f,-.421481978418013f,1.586163220440795f)));
  return float3(dot(xyz,float3(1.51667204f,-.28147805f,-.14696363f)),
                dot(xyz,float3(-.46491710f,1.25142378f,.17488461f)),
                dot(xyz,float3(.06484905f,.10913934f,.76141462f)));
}
float2 radial(float2 chroma, device const float2 *lut) {
  float2 q = clamp((chroma + 1.f) * .5f * 63.f, 0.f, 63.f);
  int2 a = int2(q), b = min(a + 1, int2(63));
  float2 t = q - float2(a);
  float2 v00 = lut[a.y * 64 + a.x], v10 = lut[a.y * 64 + b.x];
  float2 v01 = lut[b.y * 64 + a.x], v11 = lut[b.y * 64 + b.x];
  return mix(mix(v00, v10, t.x), mix(v01, v11, t.x), t.y);
}
float3 rbf(float3 input, device const float *lut) {
  float3 q = (input + .25f) * (32.f / 1.75f);
  if (any(q < 0.f) || any(q > 32.f)) return input;
  int3 a = int3(q), b = min(a + 1, int3(32));
  float3 t = q - float3(a);
  float3 v = float3(0.f);
  for (int mask=0; mask<8; ++mask) {
    int ri = mask & 1 ? b.x : a.x;
    int gi = mask & 2 ? b.y : a.y;
    int bi = mask & 4 ? b.z : a.z;
    float w = (mask & 1 ? t.x : 1.f-t.x) *
              (mask & 2 ? t.y : 1.f-t.y) *
              (mask & 4 ? t.z : 1.f-t.z);
    int index = ((ri*33+gi)*33+bi)*3;
    v += float3(lut[index],lut[index+1],lut[index+2]) * w;
  }
  return v;
}
float3 match(float3 input, constant Params &p, device const float2 *lut,
             device const float *rbfLut) {
  if (!all(isfinite(input))) return input;
  if (p.method == 2) {
    if (p.biasWeight <= 0.f) return input;
    float3 matched = rbf(input, rbfLut);
    float3 out = input + clamp(p.biasWeight,0.f,2.f)*(matched-input);
    return all(isfinite(out)) ? out : input;
  }
  float3 x = decodeDI(input);
  float y = luma(x);
  if (p.neutralAmount > 0.f && y > 1e-7f) {
    float3 z = x * exp(p.neutral.xyz * clamp(p.neutralAmount,0.f,1.f));
    float zy = luma(z);
    if (zy > 1e-7f && all(isfinite(z))) x = z * (y / zy);
  }
  if ((p.hueAmount > 0.f || p.satAmount > 0.f) &&
      (p.method != 1 || p.biasWeight > 0.f) && y > 1e-6f) {
    float3 l = toLab(x);
    float c = length(l.yz);
    if (l.x > 1e-5f && c / l.x > .005f) {
      float h = atan2(l.z,l.y);
      float dh, ds;
      if (p.method == 1) {
        float2 change = radial(l.yz/l.x, lut) * clamp(p.biasWeight,0.f,2.f);
        dh = change.x; ds = change.y;
      } else {
        dh = p.hue.x + p.hue.y*sin(h) + p.hue.z*cos(h);
        ds = p.sat.x + p.sat.y*sin(h) + p.sat.z*cos(h);
      }
      float h2 = h + clamp(p.hueAmount,0.f,1.f)*clamp(dh,-.5235987756f,.5235987756f);
      float c2 = c * exp(clamp(p.satAmount,0.f,1.f)*clamp(ds,-.6931471806f,.6931471806f));
      float3 z = fromLab(float3(l.x,c2*cos(h2),c2*sin(h2)));
      float zy = luma(z);
      if (all(isfinite(z)) && zy > 1e-6f) {
        z *= y / zy;
        if (max(max(abs(z.x),abs(z.y)),abs(z.z)) < 100.f) x = z;
      }
    }
  }
  if (p.exposureAmount > 0.f) x *= exp2(clamp(p.exposureAmount,0.f,1.f)*p.stops);
  float3 out = encodeDI(x);
  return all(isfinite(out)) ? out : input;
}
kernel void cameraMatch(device const float *src [[buffer(0)]],
                        device float *dst [[buffer(1)]],
                        constant Params &p [[buffer(2)]],
                        device const float2 *lut [[buffer(3)]],
                        device const float *rbfLut [[buffer(4)]],
                        uint2 tid [[thread_position_in_grid]]) {
  if (tid.x >= uint(p.winW) || tid.y >= uint(p.winH)) return;
  int x = p.winX + int(tid.x), y = p.winY + int(tid.y);
  if (x < p.dstX || y < p.dstY || x >= p.dstX+p.dstW || y >= p.dstY+p.dstH) return;
  int di = (y-p.dstY)*p.dstRowFloats + (x-p.dstX)*p.dstComponents;
  if (x < p.srcX || y < p.srcY || x >= p.srcX+p.srcW || y >= p.srcY+p.srcH) {
    for (int k=0;k<p.dstComponents;k++) dst[di+k]=0.f;
    return;
  }
  int si = (y-p.srcY)*p.srcRowFloats + (x-p.srcX)*p.srcComponents;
  if (p.exactCopy) {
    for (int k=0;k<p.dstComponents;k++) dst[di+k]=src[si+k];
    return;
  }
  float alpha = p.srcComponents == 4 ? src[si+3] : 1.f;
  float3 rgb = float3(src[si],src[si+1],src[si+2]);
  if (p.srcPremult && alpha > 1e-6f) rgb /= alpha;
  float3 out = match(rgb,p,lut,rbfLut);
  if (p.dstPremult) out *= alpha;
  dst[di]=out.x; dst[di+1]=out.y; dst[di+2]=out.z;
  if (p.dstComponents == 4) dst[di+3]=alpha;
}
)METAL";

std::mutex pipelineMutex;
id<MTLDevice> pipelineDevice = nil;
id<MTLComputePipelineState> pipeline = nil;
struct RbfBufferEntry {
  std::weak_ptr<const RbfLut> source;
  id<MTLDevice> device;
  id<MTLBuffer> buffer;
};
std::mutex rbfBufferMutex;
std::vector<RbfBufferEntry> rbfBuffers;
id<MTLBuffer> cachedRbfBuffer(id<MTLDevice> device,
                              const std::shared_ptr<const RbfLut> &lut) {
  std::lock_guard<std::mutex> lock(rbfBufferMutex);
  for (auto it = rbfBuffers.begin(); it != rbfBuffers.end();) {
    if (it->source.expired()) {
      [it->buffer release];
      it = rbfBuffers.erase(it);
    } else {
      if (it->device == device && it->source.lock() == lut)
        return it->buffer;
      ++it;
    }
  }
  id<MTLBuffer> buffer = [device newBufferWithBytes:lut->values.data()
                                               length:sizeof(lut->values)
                                              options:MTLResourceStorageModeShared];
  if (buffer)
    rbfBuffers.push_back({lut, device, buffer});
  return buffer;
}
}

bool renderMetal(void *queuePtr, void *source, void *output,
                 const MetalParams &p, const RadialLut *lut,
                 std::shared_ptr<const RbfLut> rbfLut) {
  @autoreleasepool {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)queuePtr;
    id<MTLBuffer> src = (__bridge id<MTLBuffer>)source;
    id<MTLBuffer> dst = (__bridge id<MTLBuffer>)output;
    if (!queue || !src || !dst || p.winW <= 0 || p.winH <= 0 ||
        p.srcRowFloats <= 0 || p.dstRowFloats <= 0)
      return false;
    id<MTLComputePipelineState> current;
    {
      std::lock_guard<std::mutex> lock(pipelineMutex);
      if (!pipeline || pipelineDevice != queue.device) {
        NSError *error = nil;
        id<MTLLibrary> library = [queue.device newLibraryWithSource:
            [NSString stringWithUTF8String:shader] options:nil error:&error];
        if (!library) {
          NSLog(@"Camera Match Metal library: %@", error);
          return false;
        }
        id<MTLFunction> function = [library newFunctionWithName:@"cameraMatch"];
        pipeline = [queue.device newComputePipelineStateWithFunction:function
                                                               error:&error];
        if (!pipeline) {
          NSLog(@"Camera Match Metal pipeline: %@", error);
          return false;
        }
        pipelineDevice = queue.device;
      }
      current = pipeline;
    }
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!command || !encoder) return false;
    float empty[2]{};
    id<MTLBuffer> rbfBuffer = rbfLut
        ? cachedRbfBuffer(queue.device, rbfLut)
        : [queue.device newBufferWithBytes:empty length:sizeof(empty)
                                  options:MTLResourceStorageModeShared];
    if (!rbfBuffer) return false;
    id<MTLBuffer> radialBuffer = lut
        ? [queue.device newBufferWithBytes:lut->values.data()
                                   length:sizeof(lut->values)
                                  options:MTLResourceStorageModeShared]
        : rbfBuffer;
    if (!radialBuffer) {
      if (!rbfLut) [rbfBuffer release];
      return false;
    }
    [encoder setComputePipelineState:current];
    [encoder setBuffer:src offset:0 atIndex:0];
    [encoder setBuffer:dst offset:0 atIndex:1];
    [encoder setBytes:&p length:sizeof(p) atIndex:2];
    [encoder setBuffer:radialBuffer offset:0 atIndex:3];
    [encoder setBuffer:rbfBuffer offset:0 atIndex:4];
    NSUInteger width = current.threadExecutionWidth;
    NSUInteger height = std::max<NSUInteger>(1, current.maxTotalThreadsPerThreadgroup / width);
    [encoder dispatchThreads:MTLSizeMake(p.winW,p.winH,1)
      threadsPerThreadgroup:MTLSizeMake(width,height,1)];
    [encoder endEncoding];
    [command commit];
    if (lut)
      [radialBuffer release];
    if (!rbfLut)
      [rbfBuffer release];
    return true;
  }
}
} // namespace cm
