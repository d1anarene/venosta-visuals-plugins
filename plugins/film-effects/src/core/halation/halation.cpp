#include <cmath>

#include "../blur/blur.h"
#include "../common/fe_kernels.h"
#include "../common/parallel.h"
#include "../pipeline/pipeline.h"

namespace fe {

// Halation: light from bright, red-weighted highlights scatters a short distance (two scales,
// radius and radius/2) and is added back mostly where the destination is NOT itself a highlight
// (fe_halation_spill), giving a warm rim around bright edges rather than a red wash.
Plane halationSpill(const Image& img, WorkingSpace space, const HalationParams& p) {
  const int w = img.width, h = img.height;
  const int sp = static_cast<int>(space);
  Plane src(w, h, 1);
  parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      const float* e = img.at(x, y);
      src.at(x, y)[0] = fe_halation_source(fe_make3(std::fmax(fe_to_linear1(e[0], sp), 0.f),
                                                    std::fmax(fe_to_linear1(e[1], sp), 0.f),
                                                    std::fmax(fe_to_linear1(e[2], sp), 0.f)),
                                           p.threshold, p.knee);
    }
  });
  const HalationPlan plan = planHalation(p, w, h);
  Plane nearB = blurByPlan(src, plan.nearStep);
  Plane farB = blurByPlan(src, plan.farStep);
  constexpr float kGain = 4.0f;  // maps intensity 0..1 to a useful visual range (same on GPU)
  for (size_t i = 0; i < src.v.size(); ++i) {
    float blurred = plan.nearWeight * nearB.v[i] + plan.farWeight * farB.v[i];
    src.v[i] = fe_halation_spill(blurred, src.v[i]) * p.intensity * kGain;  // reuse src storage
  }
  return src;
}

}  // namespace fe
