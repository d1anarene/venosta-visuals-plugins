#include <algorithm>
#include <cmath>
#include <vector>

#include "../blur/blur.h"
#include "../blur/blur_plan.h"
#include "../common/fe_kernels.h"
#include "../common/parallel.h"
#include "../pipeline/pipeline.h"

namespace fe {

// Multiscale diffusion: the soft-knee highlight light is blurred with Gaussians whose sigmas
// double each step up to `radius` (% of frame height). Each scale is a normalised blur, so
// total scattered energy is exactly intensity * highlight energy, distributed over a natural
// heavy-tailed profile (narrow core + wide skirt) instead of one flat halo.
Plane bloomEnergy(const Image& img, WorkingSpace space, const BloomParams& p, Quality q) {
  const int w = img.width, h = img.height;
  const int sp = static_cast<int>(space);
  Plane bright(w, h, 3);
  parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      const float* e = img.at(x, y);
      float c[3] = {vv_to_linear1(e[0], sp), vv_to_linear1(e[1], sp), vv_to_linear1(e[2], sp)};
      float wgt = fe_soft_knee_weight(vv_luma(vv_make3(c[0], c[1], c[2])), p.threshold, p.knee);
      float* b = bright.at(x, y);
      for (int ch = 0; ch < 3; ++ch) b[ch] = std::fmax(c[ch], 0.0f) * wgt;
    }
  });
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
      gaussianBlurAccumulate(levels[L], plan.scales[s].sigmaAtLevel, plan.weights[s], here);
    }
    acc = std::move(here);
    if (L > 0) levels.pop_back();  // finished with this level: release it early
  }
  for (float& v : acc.v) v *= p.intensity;
  return acc;
}

}  // namespace fe
