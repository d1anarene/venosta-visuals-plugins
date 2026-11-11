// Gaussian blur with clamp-to-edge borders (no dark frame edges), accelerated for large radii
// with a 2x pyramid. Deterministic: fixed evaluation order, no threading inside.
#pragma once
#include <vector>

#include "../image.h"

namespace fe {

/** Blur `src` with a Gaussian of standard deviation `sigma` (pixels) into a new plane. */
Plane gaussianBlur(const Plane& src, float sigma);

/** Normalised 1-D Gaussian weights (radius ceil(4 sigma)), shared with the GPU backends.
 *  Blurs with sigma < 0.05 are skipped entirely by every backend. */
std::vector<float> gaussianKernel(float sigma);

/** Exact separable Gaussian (reference used by tests and for small sigma). */
Plane gaussianBlurDirect(const Plane& src, float sigma);

/** acc += weight * gaussianBlurDirect(src, sigma), without allocating the blurred plane. */
void gaussianBlurAccumulate(const Plane& src, float sigma, float weight, Plane& acc);

/** [1 3 3 1] downsample by 2; odd edges are clamped. */
Plane downsample2(const Plane& src);

/** Bilinear upsample to an exact size (pixel-centre aligned). */
Plane upsampleTo(const Plane& src, int width, int height);

}  // namespace fe

#include "blur_plan.h"

namespace fe {
/** Evaluate a planned blur: downsample `step.level` times, blur, upsample back to src size. */
Plane blurByPlan(const Plane& src, const BlurStep& step);
}  // namespace fe
