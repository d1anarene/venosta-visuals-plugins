// Denoise — per-pixel GPU passes of the spatial filter (C++17 / CUDA) for the CUDA kernels and the
// CPU emulation test; the Metal shader mirrors them. Maths from dn_kernels.h / dn_color.h, shared
// with the CPU reference (dn_spatial.cpp).
#pragma once
#include "dn_color.h"
#include "dn_kernels.h"
#include "gpu/vv_gpu_pixels.h"

namespace dn {
namespace gpu {
using namespace vv::gpu;

struct FrameLayout {
  uint32_t width, height, rowPixels;
  int32_t rgba;
};

struct BandInfo {
  uint32_t width, height;
  int32_t band, hasScale;
  float tau, strength[3], recovery;
};

// 1. Host frame -> opponent (xyz, alpha in w) and RGB (xyz, alpha in w).
template <typename T>
VV_HD void pxDnPrepare(const T* src, F4* opp, F4* rgb, const FrameLayout& f, uint32_t x, uint32_t y) {
  const F4 s = loadPx(src, y * f.rowPixels + x);
  const vv_float3 c = f.rgba ? vv_make3(s.x, s.y, s.z) : vv_make3(s.z, s.y, s.x);
  const vv_float3 o = dn_to_opponent(c);
  const uint32_t i = y * f.width + x;
  opp[i] = F4{o.x, o.y, o.z, s.w};
  rgb[i] = F4{c.x, c.y, c.z, s.w};
}

// 2. One a trous smoothing step ([1 4 6 4 1]/16, holes 2^j), horizontal then vertical, clamped.
VV_HD void pxDnStarletH(const F4* src, F4* dst, PlaneInfo p, int step, uint32_t x, uint32_t y) {
  const float k[5] = {1.0f / 16, 4.0f / 16, 6.0f / 16, 4.0f / 16, 1.0f / 16};
  F4 acc{0, 0, 0, 0};
  for (int t = -2; t <= 2; ++t) {
    const F4 v = src[y * p.width + uint32_t(clampi(int(x) + t * step, 0, int(p.width) - 1))];
    acc.x += k[t + 2] * v.x; acc.y += k[t + 2] * v.y; acc.z += k[t + 2] * v.z; acc.w += k[t + 2] * v.w;
  }
  dst[y * p.width + x] = acc;
}
VV_HD void pxDnStarletV(const F4* src, F4* dst, PlaneInfo p, int step, uint32_t x, uint32_t y) {
  const float k[5] = {1.0f / 16, 4.0f / 16, 6.0f / 16, 4.0f / 16, 1.0f / 16};
  F4 acc{0, 0, 0, 0};
  for (int t = -2; t <= 2; ++t) {
    const F4 v = src[uint32_t(clampi(int(y) + t * step, 0, int(p.height) - 1)) * p.width + x];
    acc.x += k[t + 2] * v.x; acc.y += k[t + 2] * v.y; acc.z += k[t + 2] * v.z; acc.w += k[t + 2] * v.w;
  }
  dst[y * p.width + x] = acc;
}

// 3. Band = cur - next; energy = band^2 (per channel).
VV_HD void pxDnBand(const F4* cur, const F4* next, F4* band, F4* energy, PlaneInfo p, uint32_t x, uint32_t y) {
  const uint32_t i = y * p.width + x;
  const F4 b{cur[i].x - next[i].x, cur[i].y - next[i].y, cur[i].z - next[i].z, 0.0f};
  band[i] = b;
  energy[i] = F4{b.x * b.x, b.y * b.y, b.z * b.z, 0.0f};
}

// 4. Noise variance of the opponent channels at the band's lowpass colour (x noise scale^2).
VV_HD void pxDnVar(const F4* lowRgb, const F4* scale, F4* var, const DnProfileTable& t, const BandInfo& b, uint32_t x,
                   uint32_t y) {
  const uint32_t i = y * b.width + x;
  const F4 l = lowRgb[i];
  const vv_float3 v = dn_opponent_variance(&t.sigma[b.band * 3 * DN_BINS], &t.corr[b.band * 3], vv_make3(l.x, l.y, l.z));
  const float k = b.hasScale ? scale[i].x : 1.0f;
  var[i] = F4{v.x * k * k, v.y * k * k, v.z * k * k, 0.0f};
}

// 5. Stage 1: squared pilot coefficient.
VV_HD void pxDnPilot(const F4* band, const F4* E, const F4* var, F4* pilot2, const BandInfo& b, uint32_t x, uint32_t y) {
  const uint32_t i = y * b.width + x;
  pilot2[i] = F4{dn_pilot_sq(band[i].x, E[i].x, var[i].x, b.tau), dn_pilot_sq(band[i].y, E[i].y, var[i].y, b.tau),
                 dn_pilot_sq(band[i].z, E[i].z, var[i].z, b.tau), 0.0f};
}

// 6. Stage 2: out += band * gain.
VV_HD void pxDnAccumulate(F4* out, const F4* band, const F4* P, const F4* var, const BandInfo& b, uint32_t x, uint32_t y) {
  const uint32_t i = y * b.width + x;
  const float rec = b.band == 0 ? b.recovery : 0.0f;
  out[i].x += band[i].x * dn_wiener_gain(P[i].x, var[i].x, b.strength[0], rec);
  out[i].y += band[i].y * dn_wiener_gain(P[i].y, var[i].y, b.strength[1], rec);
  out[i].z += band[i].z * dn_wiener_gain(P[i].z, var[i].z, b.strength[2], rec);
}

// 7. Opponent result (bands + residual) -> host frame; alpha from the source; non-finite -> source.
template <typename T>
VV_HD void pxDnFinish(const T* src, T* dst, const F4* out, const F4* residual, const FrameLayout& f, uint32_t x,
                      uint32_t y) {
  const uint32_t i = y * f.width + x, o = y * f.rowPixels + x;
  const F4 s = loadPx(src, o);
  const vv_float3 c = dn_from_opponent(vv_make3(out[i].x + residual[i].x, out[i].y + residual[i].y, out[i].z + residual[i].z));
  float r[3] = {f.rgba ? s.x : s.z, s.y, f.rgba ? s.z : s.x};
  const float cc[3] = {c.x, c.y, c.z};
  for (int k = 0; k < 3; ++k)
    if (isFiniteF(cc[k])) r[k] = cc[k];
  if (f.rgba) storePx(dst, o, F4{r[0], r[1], r[2], s.w});
  else storePx(dst, o, F4{r[2], r[1], r[0], s.w});
}

}  // namespace gpu
}  // namespace dn
