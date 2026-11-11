#include <algorithm>
#include <cmath>
#include <vector>

#include "../blur/blur.h"
#include "../blur/blur_plan.h"
#include "../common/fe_kernels.h"
#include "../pipeline/pipeline.h"

namespace fe {

// Multiscale diffusion: the soft-knee highlight light is blurred with Gaussians whose sigmas
// double each step up to `radius` (% of frame height). Each scale is a normalised blur, so
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
  const BloomPlan plan = planBloom(p, q, w, h);
  const int scales = static_cast<int>(plan.scales.size());
  const int maxLevel = plan.maxLevel;
  std::vector<Plane> levels;
  levels.push_back(std::move(bright));
  for (int L = 1; L <= maxLevel; ++L) levels.push_back(downsample2(levels.back()));

  Plane acc;
  for (int L = maxLevel; L >= 0; --L) {
    Plane here = acc.v.empty() ? Plane(levels[L].width, levels[L].height, 3)
                               : upsampleTo(acc, levels[L].width, levels[L].height);
    for (int s = 0; s < scales; ++s) {
      if (plan.scales[s].level != L) continue;
      Plane b = gaussianBlurDirect(levels[L], plan.scales[s].sigmaAtLevel);
      for (size_t i = 0; i < here.v.size(); ++i) here.v[i] += plan.weights[s] * b.v[i];
    }
    acc = std::move(here);
  }
  for (float& v : acc.v) v *= p.intensity;
  return acc;
}

}  // namespace fe
