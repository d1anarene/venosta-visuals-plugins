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
  int32_t view;  // 0 result, 1 removed noise (0.5 + 4 x (input - result)), 2 input
  float mix;     // result only: input + (result - input) * mix
};

struct BandInfo {
  uint32_t width, height;
  int32_t band, hasScale;
  float tau, strength[3], recovery;
};

// Noise estimate on the device: a centred crop of the frame as RGB, top row first (same pixels as
// the CPU reference's centreCrop of the top-first image), its starlet decomposition, and the
// 8x8 / stride 4 block statistics of every band (dn_profile.h, EstimateBlock). The statistical
// part (quantiles, iterations) stays on the CPU (estimateFromBlocks).
struct CropInfo {
  uint32_t width, height;          // crop size
  uint32_t frameHeight, rowPixels;
  uint32_t x0, y0;                 // crop origin in top-first coordinates
  int32_t rgba, bottomUp;
};
struct BlockGrid {
  uint32_t width, height, cols, rows;  // plane size, blocks per row / column
};

template <typename T>
VV_HD void pxDnCrop(const T* src, F4* rgb, const CropInfo& c, uint32_t x, uint32_t y) {
  const uint32_t row = c.bottomUp ? c.frameHeight - 1u - (c.y0 + y) : c.y0 + y;
  const F4 s = loadPx(src, row * c.rowPixels + c.x0 + x);
  rgb[y * c.width + x] = c.rgba ? F4{s.x, s.y, s.z, 0.0f} : F4{s.z, s.y, s.x, 0.0f};
}

// Vertical starlet step alone (the estimate needs no band pass of its own).
VV_HD void pxDnStarletV(const F4* src, F4* dst, PlaneInfo p, int step, uint32_t x, uint32_t y) {
  const float k[5] = {1.0f / 16, 4.0f / 16, 6.0f / 16, 4.0f / 16, 1.0f / 16};
  F4 acc{0, 0, 0, 0};
  for (int t = -2; t <= 2; ++t) {
    const F4 v = src[uint32_t(clampi(int(y) + t * step, 0, int(p.height) - 1)) * p.width + x];
    acc.x += k[t + 2] * v.x; acc.y += k[t + 2] * v.y; acc.z += k[t + 2] * v.z; acc.w += k[t + 2] * v.w;
  }
  dst[y * p.width + x] = acc;
}

// One block (column q, row r): band = cur - next, lowpass = next. Out: 3 F4 per block
// {var.rgb, value.rgb, cov RG RB GB}.
VV_HD void pxDnBlock(const F4* cur, const F4* next, F4* out, const BlockGrid& g, uint32_t q, uint32_t r) {
  float s2[3] = {0, 0, 0}, sv[3] = {0, 0, 0}, sx[3] = {0, 0, 0};
  for (uint32_t y = r * 4u; y < r * 4u + 8u; ++y)
    for (uint32_t x = q * 4u; x < q * 4u + 8u; ++x) {
      const uint32_t i = y * g.width + x;
      const F4 c = cur[i], l = next[i];
      const float b0 = c.x - l.x, b1 = c.y - l.y, b2 = c.z - l.z;
      s2[0] += b0 * b0; s2[1] += b1 * b1; s2[2] += b2 * b2;
      sv[0] += l.x; sv[1] += l.y; sv[2] += l.z;
      sx[0] += b0 * b1; sx[1] += b0 * b2; sx[2] += b1 * b2;
    }
  const float n = 1.0f / 64.0f;
  const uint32_t o = (r * g.cols + q) * 3u;
  out[o] = F4{s2[0] * n, s2[1] * n, s2[2] * n, 0.0f};
  out[o + 1] = F4{sv[0] * n, sv[1] * n, sv[2] * n, 0.0f};
  out[o + 2] = F4{sx[0] * n, sx[1] * n, sx[2] * n, 0.0f};
}

// 1. Host frame -> opponent (xyz, alpha in w).
template <typename T>
VV_HD void pxDnPrepare(const T* src, F4* opp, const FrameLayout& f, uint32_t x, uint32_t y) {
  const F4 s = loadPx(src, y * f.rowPixels + x);
  const vv_float3 c = f.rgba ? vv_make3(s.x, s.y, s.z) : vv_make3(s.z, s.y, s.x);
  const vv_float3 o = dn_to_opponent(c);
  opp[y * f.width + x] = F4{o.x, o.y, o.z, s.w};
}

// 2. One a trous smoothing step ([1 4 6 4 1]/16, holes 2^j), horizontal, clamped.
VV_HD void pxDnStarletH(const F4* src, F4* dst, PlaneInfo p, int step, uint32_t x, uint32_t y) {
  const float k[5] = {1.0f / 16, 4.0f / 16, 6.0f / 16, 4.0f / 16, 1.0f / 16};
  F4 acc{0, 0, 0, 0};
  for (int t = -2; t <= 2; ++t) {
    const F4 v = src[y * p.width + uint32_t(clampi(int(x) + t * step, 0, int(p.width) - 1))];
    acc.x += k[t + 2] * v.x; acc.y += k[t + 2] * v.y; acc.z += k[t + 2] * v.z; acc.w += k[t + 2] * v.w;
  }
  dst[y * p.width + x] = acc;
}

// 3. Vertical step fused with the band: next = V(tmp); band = cur - next; energy = band^2; noise
// variance of the opponent channels at the band's lowpass colour. The starlet is linear and the
// opponent transform orthonormal, so the RGB lowpass is the inverse transform of the opponent
// lowpass (same maths as a separate RGB decomposition, float rounding aside).
VV_HD void pxDnStarletVBand(const F4* tmp, const F4* cur, F4* next, F4* band, F4* energy, F4* var,
                            const DnProfileTable& t, const BandInfo& b, int step, uint32_t x, uint32_t y) {
  const float k[5] = {1.0f / 16, 4.0f / 16, 6.0f / 16, 4.0f / 16, 1.0f / 16};
  F4 acc{0, 0, 0, 0};
  for (int s = -2; s <= 2; ++s) {
    const F4 v = tmp[uint32_t(clampi(int(y) + s * step, 0, int(b.height) - 1)) * b.width + x];
    acc.x += k[s + 2] * v.x; acc.y += k[s + 2] * v.y; acc.z += k[s + 2] * v.z; acc.w += k[s + 2] * v.w;
  }
  const uint32_t i = y * b.width + x;
  next[i] = acc;
  const F4 c = cur[i];
  const F4 d{c.x - acc.x, c.y - acc.y, c.z - acc.z, 0.0f};
  band[i] = d;
  energy[i] = F4{d.x * d.x, d.y * d.y, d.z * d.z, 0.0f};
  const vv_float3 low = dn_from_opponent(vv_make3(acc.x, acc.y, acc.z));
  const vv_float3 v = dn_opponent_variance(&t.sigma[b.band * 3 * DN_BINS], &t.corr[b.band * 3], low);
  var[i] = F4{v.x, v.y, v.z, 0.0f};
}

// 4. Stage 1: squared pilot coefficient.
VV_HD void pxDnPilot(const F4* band, const F4* E, const F4* var, F4* pilot2, const BandInfo& b, uint32_t x, uint32_t y) {
  const uint32_t i = y * b.width + x;
  pilot2[i] = F4{dn_pilot_sq(band[i].x, E[i].x, var[i].x, b.tau), dn_pilot_sq(band[i].y, E[i].y, var[i].y, b.tau),
                 dn_pilot_sq(band[i].z, E[i].z, var[i].z, b.tau), 0.0f};
}

// 5. Stage 2: out += band * gain.
VV_HD void pxDnAccumulate(F4* out, const F4* band, const F4* P, const F4* var, const BandInfo& b, uint32_t x, uint32_t y) {
  const uint32_t i = y * b.width + x;
  const float rec = b.band == 0 ? b.recovery : 0.0f;
  out[i].x += band[i].x * dn_wiener_gain(P[i].x, var[i].x, b.strength[0], rec);
  out[i].y += band[i].y * dn_wiener_gain(P[i].y, var[i].y, b.strength[1], rec);
  out[i].z += band[i].z * dn_wiener_gain(P[i].z, var[i].z, b.strength[2], rec);
}

// 6. Opponent result (bands + residual) -> host frame; alpha from the source; non-finite -> source.
template <typename T>
VV_HD void pxDnFinish(const T* src, T* dst, const F4* out, const F4* residual, const FrameLayout& f, uint32_t x,
                      uint32_t y) {
  const uint32_t i = y * f.width + x, o = y * f.rowPixels + x;
  const F4 s = loadPx(src, o);
  const vv_float3 c = dn_from_opponent(vv_make3(out[i].x + residual[i].x, out[i].y + residual[i].y, out[i].z + residual[i].z));
  const float in[3] = {f.rgba ? s.x : s.z, s.y, f.rgba ? s.z : s.x};
  float r[3] = {in[0], in[1], in[2]};
  const float cc[3] = {c.x, c.y, c.z};
  for (int k = 0; k < 3; ++k) {
    float v = f.view == 1 ? 0.5f + 4.0f * (in[k] - cc[k]) : (f.view == 2 ? in[k] : cc[k]);
    if (f.view == 0 && f.mix < 1.0f) v = in[k] + (cc[k] - in[k]) * f.mix;
    if (isFiniteF(v)) r[k] = v;
  }
  if (f.rgba) storePx(dst, o, F4{r[0], r[1], r[2], s.w});
  else storePx(dst, o, F4{r[2], r[1], r[0], s.w});
}

}  // namespace gpu
}  // namespace dn
