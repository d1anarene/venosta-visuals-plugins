#include <cmath>

#include "../blur/blur.h"
#include "../common/fe_kernels.h"
#include "../pipeline/pipeline.h"

namespace fe {

// Halation: light from bright, red-weighted highlights scatters a short distance (two scales,
// radius and radius/2) and is added back mostly where the destination is NOT itself a highlight
// (fe_halation_spill), giving a warm rim around bright edges rather than a red wash.
Plane halationEnergy(const Plane& lin, const HalationParams& p) {
  const int w = lin.width, h = lin.height;
  Plane src(w, h, 1);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    const float* c = &lin.v[i * 3];
    src.v[i] = fe_halation_source(fe_make3(std::fmax(c[0], 0.f), std::fmax(c[1], 0.f), std::fmax(c[2], 0.f)),
                                  p.threshold, p.knee);
  }
  const HalationPlan plan = planHalation(p, w, h);
  Plane nearB = blurByPlan(src, plan.nearStep);
  Plane farB = blurByPlan(src, plan.farStep);
  const fe_float3 tint = fe_halation_tint(p.warmth);
  constexpr float kGain = 4.0f;  // maps intensity 0..1 to a useful visual range
  Plane out(w, h, 3);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    float blurred = plan.nearWeight * nearB.v[i] + plan.farWeight * farB.v[i];
    float spill = fe_halation_spill(blurred, src.v[i]) * p.intensity * kGain;
    out.v[i * 3 + 0] = spill * tint.x;
    out.v[i * 3 + 1] = spill * tint.y;
    out.v[i * 3 + 2] = spill * tint.z;
  }
  return out;
}

}  // namespace fe
