// Denoise — Laplacian pyramid on the engine's [1 3 3 1] downsample / bilinear upsample.
// band[b] = level[b] - up(level[b+1]); the last entry is the low-pass residual. Reconstruction is
// exact (up to float rounding) by construction. Used to define the noise profile (noise sigma per
// band) and by the spatial denoiser candidates.
#pragma once
#include <vector>

#include "vv/image.h"

namespace dn {

struct Pyramid {
  std::vector<vv::Plane> bands;  // bands.size() = levels + 1 (last = low-pass residual)
};

Pyramid decompose(const vv::Plane& img, int levels);
vv::Plane reconstruct(const Pyramid& p);

/** Low-pass image used to bin band b by value: level[b+1] upsampled to band b's size (all
 *  channels). */
vv::Plane bandLowpass(const vv::Plane& img, int band);

}  // namespace dn
