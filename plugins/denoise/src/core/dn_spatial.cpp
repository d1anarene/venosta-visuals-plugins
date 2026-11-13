#include "dn_spatial.h"

#include <algorithm>
#include <cmath>

#include "dn_color.h"
#include "dn_starlet.h"
#include "vv/blur.h"
#include "vv/parallel.h"

namespace dn {

vv::Image denoiseSpatial(const vv::Image& noisy, const NoiseProfile& prof, const SpatialParams& sp) {
  const int w = noisy.width, h = noisy.height;
  if (w <= 0 || h <= 0) return noisy;
  const vv::Plane opp = toOpponent(noisy), rgb = toRgbPlane(noisy);
  Starlet so = starletDecompose(opp, kBands);
  const Starlet sr = starletDecompose(rgb, kBands);  // lowpass colour for the noise lookup
  const float strength[3] = {sp.lumaStrength, sp.chromaStrength, sp.chromaStrength};
  for (int j = 0; j < kBands; ++j) {
    vv::Plane& band = so.bands[size_t(j)];
    const vv::Plane& low = sr.smooth[size_t(j)];
    // Noise variance per pixel and opponent channel.
    vv::Plane var(w, h, 3), e(w, h, 3);
    vv::parallelRows(h, [&](int y) {
      for (int x = 0; x < w; ++x) {
        const float* l = low.at(x, y);
        const vv_float3 s = prof.opponentSigma(j, vv_make3(l[0], l[1], l[2]));
        float* v = var.at(x, y);
        v[0] = s.x * s.x; v[1] = s.y * s.y; v[2] = s.z * s.z;
        const float* q = band.at(x, y);
        float* ee = e.at(x, y);
        for (int c = 0; c < 3; ++c) ee[c] = q[c] * q[c];
      }
    });
    // Window grows with the band scale (coefficients are correlated over ~2^j px).
    const float win = sp.window * float(1 << j);
    const vv::Plane E = vv::gaussianBlur(e, win);
    vv::Plane pilot2(w, h, 3);
    for (size_t i = 0; i < band.v.size(); ++i) {
      const float En = std::max(E.v[i], 1e-20f), sig = std::max(En - var.v[i], 0.f);
      const float p = band.v[i] * sig / En;
      pilot2.v[i] = p * p;
    }
    const vv::Plane P = vv::gaussianBlur(pilot2, win);
    for (size_t i = 0; i < band.v.size(); ++i) {
      const int c = int(i % 3);
      const float denom = P.v[i] + strength[c] * var.v[i];
      float g = denom > 0.f ? P.v[i] / denom : 1.f;
      if (j == 0 && sp.detailRecovery > 0.f) g = g + sp.detailRecovery * (1.f - g);
      band.v[i] *= std::isfinite(g) ? g : 1.f;
    }
  }
  vv::Image out = fromOpponent(starletReconstruct(so), noisy);
  for (size_t i = 0; i < out.px.size(); ++i)
    if (!std::isfinite(out.px[i])) out.px[i] = noisy.px[i];
  return out;
}

}  // namespace dn
