// Gaussian blur with clamp-to-edge borders (no dark frame edges), accelerated for large radii
// with a 2x pyramid. Deterministic: fixed evaluation order, no threading inside.
#pragma once
#include "../image.h"

namespace fe {

/** Blur `src` with a Gaussian of standard deviation `sigma` (pixels) into a new plane. */
Plane gaussianBlur(const Plane& src, float sigma);

/** Exact separable Gaussian (reference used by tests and for small sigma). */
Plane gaussianBlurDirect(const Plane& src, float sigma);

/** 2x2 box downsample; odd edges are clamped. */
Plane downsample2(const Plane& src);

/** Bilinear upsample to an exact size (pixel-centre aligned). */
Plane upsampleTo(const Plane& src, int width, int height);

}  // namespace fe
