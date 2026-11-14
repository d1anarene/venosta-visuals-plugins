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

vv::Image halveImage(const vv::Image& img) {
  const int w2 = (img.width + 1) / 2, h2 = (img.height + 1) / 2;
  vv::Image out(w2, h2);
  for (int y = 0; y < h2; ++y)
    for (int x = 0; x < w2; ++x) {
      const int x0 = 2 * x, y0 = 2 * y, x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
      const float *a = img.at(x0, y0), *b = img.at(x1, y0), *c = img.at(x0, y1), *d = img.at(x1, y1);
      float* o = out.at(x, y);
      for (int k = 0; k < 4; ++k) o[k] = ((a[k] + b[k]) + (c[k] + d[k])) * 0.25f;
    }
  return out;
}

vv::Image upsampleImage(const vv::Image& low, int width, int height) {
  vv::Image out(width, height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const float u = float(x) * 0.5f - 0.25f, v = float(y) * 0.5f - 0.25f;
      const int i0 = int(u + 1.0f) - 1, j0 = int(v + 1.0f) - 1;  // floor for u, v >= -0.25
      const float fx = u - float(i0), fy = v - float(j0);
      const int xa = std::clamp(i0, 0, low.width - 1), xb = std::clamp(i0 + 1, 0, low.width - 1);
      const int ya = std::clamp(j0, 0, low.height - 1), yb = std::clamp(j0 + 1, 0, low.height - 1);
      const float *a = low.at(xa, ya), *b = low.at(xb, ya), *c = low.at(xa, yb), *d = low.at(xb, yb);
      float* o = out.at(x, y);
      for (int k = 0; k < 4; ++k) {
        const float top = a[k] + (b[k] - a[k]) * fx, bot = c[k] + (d[k] - c[k]) * fx;
        o[k] = top + (bot - top) * fy;
      }
    }
  return out;
}

}  // namespace dn
