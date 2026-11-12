// Denoise evaluation — realistic camera-like noise on a clean image, with ground truth.
// Linear light -> camera RGB (inverse of a typical camera colour matrix) -> Bayer RGGB mosaic -> Poisson-Gaussian noise (variance shot * L + read^2,
// Gaussian approximation) -> bilinear demosaic -> colour matrix (correlates channel noise) -> optional unsharp sharpening -> encoded.
// `clean` is the same chain without noise, so noisy - clean is pure (processed) noise.
// Deterministic per seed and frame (hash-based normal deviates).
#pragma once
#include <cstdint>

#include "vv/image.h"
#include "vv/kernels.h"

namespace dn {
namespace eval {

struct NoiseSim {
  float shot = 0.002f;   // variance per unit of linear signal
  float read = 0.004f;   // standard deviation, linear
  bool bayer = true;     // mosaic + demosaic (spatially correlated noise)
  bool matrix = true;    // camera colour matrix (channel-correlated noise)
  float sharpen = 0.f;   // unsharp-mask amount (0 = off), radius 1 px
  int space = VV_SPACE_REC709_G24;
};

struct Synth {
  vv::Image noisy, clean;
};

/** Standard-normal deviate for (x, y, k) and a key (Box-Muller on two hashes). */
float gauss(int x, int y, int k, uint32_t key);

Synth synthesize(const vv::Image& source, const NoiseSim& sim, uint32_t seed, int64_t frame = 0);

}  // namespace eval
}  // namespace dn
