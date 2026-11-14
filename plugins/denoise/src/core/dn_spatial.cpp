#include "dn_spatial.h"

#include <algorithm>
#include <cmath>

#include "dn_color.h"
#include "dn_kernels.h"
#include "dn_starlet.h"
#include "vv/blur.h"
#include "vv/parallel.h"

namespace dn {

vv::Image denoiseSpatial(const vv::Image& noisy, const NoiseProfile& prof, const SpatialParams& sp,
                         const vv::Plane* noiseScale) {
  const int w = noisy.width, h = noisy.height;
  if (w <= 0 || h <= 0) return noisy;
  const vv::Plane opp = toOpponent(noisy), rgb = toRgbPlane(noisy);
  Starlet so = starletDecompose(opp, kBands);
  const Starlet sr = starletDecompose(rgb, kBands);  // lowpass colour for the noise lookup
  const float strength[3] = {sp.lumaStrength, sp.chromaStrength, sp.chromaStrength};
  static_assert(kBands == DN_BANDS && kBins == DN_BINS, "dn_kernels.h table size");
  for (int j = 0; j < kBands; ++j) {
    vv::Plane& band = so.bands[size_t(j)];
    const vv::Plane& low = sr.smooth[size_t(j)];
    // Noise variance per pixel and opponent channel (dn_kernels.h, shared with the GPU).
    vv::Plane var(w, h, 3), e(w, h, 3);
    vv::parallelRows(h, [&](int y) {
      for (int x = 0; x < w; ++x) {
        const float* l = low.at(x, y);
        const vv_float3 v3 = dn_opponent_variance(&prof.sigma[j][0][0], prof.corr[j], vv_make3(l[0], l[1], l[2]));
        float* v = var.at(x, y);
        const float k = noiseScale ? noiseScale->at(x, y)[0] : 1.f;
        v[0] = v3.x * k * k; v[1] = v3.y * k * k; v[2] = v3.z * k * k;
        const float* q = band.at(x, y);
        float* ee = e.at(x, y);
        for (int c = 0; c < 3; ++c) ee[c] = q[c] * q[c];
      }
    });
    // Window grows with the band scale (coefficients are correlated over ~2^j px).
    const float win = sp.window * float(1 << j);
    // Same blur plan as the GPU pyramid (vv_gpu_pyramid.h), so CPU and GPU agree.
    const vv::BlurStep plan = vv::planGaussian(win, w, h);
    const vv::Plane E = vv::blurByPlan(e, plan);
    vv::Plane pilot2(w, h, 3);
    for (size_t i = 0; i < band.v.size(); ++i) pilot2.v[i] = dn_pilot_sq(band.v[i], E.v[i], var.v[i], sp.pilotTau);
    const vv::Plane P = vv::blurByPlan(pilot2, plan);
    for (size_t i = 0; i < band.v.size(); ++i) {
      const int c = int(i % 3);
      band.v[i] *= dn_wiener_gain(P.v[i], var.v[i], strength[c], j == 0 ? sp.detailRecovery : 0.f);
    }
  }
  vv::Image out = fromOpponent(starletReconstruct(so), noisy);
  for (size_t i = 0; i < out.px.size(); ++i)
    if (!std::isfinite(out.px[i])) out.px[i] = noisy.px[i];
  return out;
}

}  // namespace dn
