#include <cmath>

#include "../blur/blur.h"
#include "../common/fe_kernels.h"
#include "../pipeline/pipeline.h"

namespace fe {

// Multiscale diffusion: the soft-knee highlight light is blurred with a chain of Gaussians whose
// sigmas double each step up to `radius` (% of frame height). Each scale is a normalised blur, so
// total scattered energy is exactly intensity * highlight energy, distributed over a natural
// heavy-tailed profile (narrow core + wide skirt) instead of one flat halo.
Plane bloomEnergy(const Plane& lin, const BloomParams& p, Quality q) {
  const int w = lin.width, h = lin.height;
  Plane bright(w, h, 3);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    const float* c = &lin.v[i * 3];
    float wgt = fe_soft_knee_weight(fe_luma(fe_make3(c[0], c[1], c[2])), p.threshold, p.knee);
    for (int ch = 0; ch < 3; ++ch) bright.v[i * 3 + ch] = std::fmax(c[ch], 0.0f) * wgt;
  }
  const float sigmaMax = std::fmax(p.radius / 100.0f * h, 0.5f);
  const int scales = q == Quality::High ? 6 : 5;
  // weights grow towards wide scales (sum 1); finest scale keeps the core around light sources
  static const float kW5[5] = {0.10f, 0.15f, 0.20f, 0.25f, 0.30f};
  static const float kW6[6] = {0.08f, 0.12f, 0.15f, 0.18f, 0.22f, 0.25f};
  const float* weights = scales == 6 ? kW6 : kW5;

  Plane acc(w, h, 3);
  Plane cur = bright;
  float prevSigma = 0.0f;
  for (int s = 0; s < scales; ++s) {
    float sigma = sigmaMax / std::pow(2.0f, static_cast<float>(scales - 1 - s));
    // incremental blur: blur(blur(x, a), b) == blur(x, sqrt(a^2 + b^2))
    float step = std::sqrt(std::fmax(sigma * sigma - prevSigma * prevSigma, 0.0f));
    cur = gaussianBlur(cur, step);
    prevSigma = sigma;
    for (size_t i = 0; i < acc.v.size(); ++i) acc.v[i] += weights[s] * cur.v[i];
  }
  for (float& v : acc.v) v *= p.intensity;
  return acc;
}

}  // namespace fe
