#pragma once
#include "Chart.h"
#include "ImageView.h"
#include "Match.h"
namespace cm {
struct SamplingImage {
  FloatImageView pixels;
  double par = 1;
  bool premult = false;
};
Observation samplePatch(const SamplingImage &image, const Geometry &geometry,
                        const Homography &homography, const Patch &patch,
                        const SampleEdit &edit, Point renderScale);
} // namespace cm
