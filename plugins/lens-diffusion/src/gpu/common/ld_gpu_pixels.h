// Lens & Diffusion — per-pixel GPU passes (C++17 and CUDA C++), used by the CUDA kernels and the
// CPU emulation test. The Metal shader (src/gpu/metal/LensDiffusion.metal) mirrors them.
// All geometry here is symmetric under a vertical flip, so OpenFX's bottom-up rows need no special
// handling (only the RGBA/BGRA channel order matters).
#pragma once
#include "gpu/vv_gpu_pixels.h"
#include "ld_kernels.h"

namespace ld {
namespace gpu {
using namespace vv::gpu;

struct FrameInfo {
  uint32_t width, height, rowPixels;
  int32_t space, rgba;
};
struct AberrationInfo {
  uint32_t width, height;
  float cornerPx, bias;
};
struct WeightInfo {
  uint32_t width, height;
  int32_t doDiffusion, doStreaks;
  float bias, threshold, knee;
};
struct CompositeInfo {
  uint32_t width, height, rowPixels;
  int32_t space, rgba;
  int32_t doDiffusion, doStreaks, doVignette, vignetteStyle;
  float amount, diffTint[3];
  float intensity, streakTint[3];
  float vigAmount, vigSize, vigRoundness, vigFeather;
  float mix;
};

// 1. Host frame -> linear RGB (alpha kept in w).
template <typename T>
VV_HD void pxLdPrepare(const T* src, F4* lin, const FrameInfo& fi, uint32_t x, uint32_t y) {
  F4 s = loadPx(src, y * fi.rowPixels + x);
  float r = fi.rgba ? s.x : s.z, b = fi.rgba ? s.z : s.x;
  lin[y * fi.width + x] = F4{vv_to_linear1(r, fi.space), vv_to_linear1(s.y, fi.space), vv_to_linear1(b, fi.space), s.w};
}

// 2. Lateral chromatic aberration: red/blue resampled (Catmull-Rom) at radially displaced positions.
VV_HD float caSample(const F4* lin, uint32_t w, uint32_t h, float sx, float sy, int c) {
  float bx = vv_floor(sx), by = vv_floor(sy), tx = sx - bx, ty = sy - by;
  float acc = 0.0f;
  for (int j = 0; j < 4; ++j) {
    int yy = clampi(int(by) - 1 + j, 0, int(h) - 1);
    float wy = vv_catmull_rom(ty, j);
    for (int i = 0; i < 4; ++i) {
      int xx = clampi(int(bx) - 1 + i, 0, int(w) - 1);
      F4 v = lin[uint32_t(yy) * w + uint32_t(xx)];
      acc += wy * vv_catmull_rom(tx, i) * (c == 0 ? v.x : v.z);
    }
  }
  return acc;
}
VV_HD void pxLdAberrate(const F4* lin, F4* out, const AberrationInfo& a, uint32_t x, uint32_t y) {
  const float cx = float(a.width) * 0.5f, cy = float(a.height) * 0.5f;
  const float rc = vv_sqrt(cx * cx + cy * cy);
  const float vx = float(x) + 0.5f - cx, vy = float(y) + 0.5f - cy;
  const float r = vv_sqrt(vx * vx + vy * vy);
  const F4 g = lin[y * a.width + x];
  float rb[2];
  for (int k = 0; k < 2; ++k) {
    const float sgn = k == 0 ? 1.0f : -1.0f;  // red outward, blue inward
    const float shift = sgn * ld_aberration_shift(r / rc, a.cornerPx, a.bias);
    const float m = r > 1e-6f ? 1.0f - shift / r : 1.0f;
    rb[k] = caSample(lin, a.width, a.height, cx + vx * m - 0.5f, cy + vy * m - 0.5f, k == 0 ? 0 : 2);
  }
  out[y * a.width + x] = F4{rb[0], g.y, rb[1], g.w};
}

// 3. Light that scatters (diffusion) and highlight light (streaks).
VV_HD void pxLdWeights(const F4* lin, F4* s, F4* b, const WeightInfo& wi, uint32_t x, uint32_t y) {
  const uint32_t i = y * wi.width + x;
  const F4 c = lin[i];
  const float cc[3] = {vv_max(c.x, 0.0f), vv_max(c.y, 0.0f), vv_max(c.z, 0.0f)};
  const float lum = vv_luma(vv_make3(cc[0], cc[1], cc[2]));
  if (wi.doDiffusion) {
    const float w = ld_diffusion_weight(lum, wi.bias);
    s[i] = F4{cc[0] * w, cc[1] * w, cc[2] * w, 0.0f};
  }
  if (wi.doStreaks) {
    const float w = vv_soft_knee_weight(lum, wi.threshold, wi.knee);
    b[i] = F4{cc[0] * w, cc[1] * w, cc[2] * w, 0.0f};
  }
}

// 4. Diffusion + streaks + vignette, encode, mix against the source; alpha untouched.
template <typename T>
VV_HD void pxLdComposite(const T* src, T* dst, const F4* lin, const F4* s, const F4* scat, const F4* streak,
                         const CompositeInfo& ci, uint32_t x, uint32_t y) {
  const uint32_t fi = y * ci.rowPixels + x, pi = y * ci.width + x;
  const F4 sp = loadPx(src, fi);
  const float orig[3] = {ci.rgba ? sp.x : sp.z, sp.y, ci.rgba ? sp.z : sp.x};
  float o[3] = {orig[0], orig[1], orig[2]};
  if (sp.w > 0.0f) {
    const F4 l4 = lin[pi];
    float l[3] = {l4.x, l4.y, l4.z};
    if (ci.doDiffusion) {
      const F4 a = scat[pi], b = s[pi];
      l[0] += ci.amount * (a.x * ci.diffTint[0] - b.x);
      l[1] += ci.amount * (a.y * ci.diffTint[1] - b.y);
      l[2] += ci.amount * (a.z * ci.diffTint[2] - b.z);
    }
    if (ci.doStreaks) {
      const F4 h = streak[pi];
      l[0] += ci.intensity * ci.streakTint[0] * h.x;
      l[1] += ci.intensity * ci.streakTint[1] * h.y;
      l[2] += ci.intensity * ci.streakTint[2] * h.z;
    }
    float g = 1.0f;
    if (ci.doVignette) {
      // Top-down row; the vignette is symmetric, so bottom-up buffers give the same result.
      const float d = ld_vignette_distance(float(x), float(y), float(ci.width), float(ci.height), ci.vigRoundness);
      g = ld_vignette_gain(d, ci.vigAmount, ci.vigSize, ci.vigFeather, ci.vignetteStyle);
    }
    for (int c = 0; c < 3; ++c) {
      const float e = vv_from_linear1(l[c] * g, ci.space);
      const float v = ci.mix >= 1.0f ? e : orig[c] + (e - orig[c]) * ci.mix;
      o[c] = isFiniteF(v) ? v : orig[c];
    }
  }
  if (ci.rgba) storePx(dst, fi, F4{o[0], o[1], o[2], sp.w});
  else storePx(dst, fi, F4{o[2], o[1], o[0], sp.w});
}

}  // namespace gpu
}  // namespace ld
