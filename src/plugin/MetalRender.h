#pragma once
#include "Match.h"
#include <cstdint>

namespace cm {
struct alignas(16) MetalParams {
  float rbfCenter[32][4], rbfWeight[32][4], rbfAffine[4][4];
  float rbfInvSupportSq;
  int32_t rbfCount;
  int32_t rbfSpace;
  float satAmount, exposureAmount;
  float biasWeight;
  int32_t srcX, srcY, srcW, srcH;
  int32_t dstX, dstY, dstW, dstH;
  int32_t winX, winY, winW, winH;
  int32_t srcRowFloats, dstRowFloats;
  int32_t srcComponents, dstComponents;
  int32_t srcPremult, dstPremult;
  int32_t exactCopy;
};

bool renderMetal(void *queue, void *source, void *output,
                 const MetalParams &params);
} // namespace cm
