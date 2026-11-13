// Denoise — undecimated "a trous" wavelet transform (starlet, B3-spline kernel [1 4 6 4 1]/16 with
// holes). Every band has the full frame size, so the transform is shift invariant (no aliasing or
// block artefacts when coefficients are modified, unlike a decimated pyramid). band[j] = c[j] -
// c[j+1] with c[0] = image; reconstruction is exactly the sum of all bands plus the last c.
#pragma once
#include <vector>

#include "vv/image.h"

namespace dn {

struct Starlet {
  std::vector<vv::Plane> bands;    // detail bands, finest first
  std::vector<vv::Plane> smooth;   // smooth[j] = c[j+1] (the lowpass below band j); last = residual
};

/** One a trous smoothing step at scale j (hole spacing 2^j), clamp-to-edge. */
vv::Plane starletSmooth(const vv::Plane& p, int j);
Starlet starletDecompose(const vv::Plane& img, int levels);
/** Sum of bands + residual. */
vv::Plane starletReconstruct(const Starlet& s);

}  // namespace dn
