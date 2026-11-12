// Venosta Visuals engine — generic per-pixel GPU passes (pyramid, separable Gaussian, half floats),
// shared by every plugin's CUDA kernels and the CPU emulation used in tests. Valid C++17 and CUDA
// C++. The Metal versions of the same passes are in metal/vv_common.metal.
//
// Each function computes ONE output pixel (x, y), exactly like one GPU thread. The CUDA kernels
// are thin wrappers; the emulation calls them in plain loops.
#pragma once
#include <cstdint>
#include <cstring>

#include "vv/kernels.h"

#ifdef __CUDACC__
#define VV_HD __host__ __device__ inline
#else
#define VV_HD inline
#endif

namespace vv {
namespace gpu {

struct alignas(16) F4 {
  float x, y, z, w;
};

/** Half-float pixel (Premiere GPU_BGRA_4444_16f): four IEEE binary16 values. */
struct alignas(8) H4 {
  uint16_t x, y, z, w;
};

// Portable IEEE half <-> float (round to nearest even), identical on host and device.
VV_HD float halfToFloat(uint16_t h) {
  uint32_t sign = (h & 0x8000u) << 16, exp = (h >> 10) & 0x1Fu, man = h & 0x3FFu;
  uint32_t bits;
  if (exp == 0) {
    if (man == 0) {
      bits = sign;
    } else {  // subnormal
      int e = -1;
      do { ++e; man <<= 1; } while ((man & 0x400u) == 0);
      bits = sign | (uint32_t(127 - 15 - e) << 23) | ((man & 0x3FFu) << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (man << 13);
  } else {
    bits = sign | ((exp + 112u) << 23) | (man << 13);
  }
  float f;
  memcpy(&f, &bits, 4);
  return f;
}

VV_HD uint16_t floatToHalf(float f) {
  uint32_t x;
  memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000u;
  uint32_t absx = x & 0x7FFFFFFFu;
  if (absx >= 0x7F800000u) return uint16_t(sign | 0x7C00u | (absx > 0x7F800000u ? 0x200u : 0u));  // inf/nan
  if (absx >= 0x477FF000u) return uint16_t(sign | 0x7C00u);  // overflow -> inf
  if (absx < 0x38800000u) {                                   // subnormal or zero
    if (absx < 0x33000000u) return uint16_t(sign);
    uint32_t e = absx >> 23, m = (absx & 0x7FFFFFu) | 0x800000u;
    uint32_t shift = 126 - e;  // 113..125 range handled
    uint32_t half = m >> shift;
    uint32_t rem = m & ((1u << shift) - 1u), mid = 1u << (shift - 1);
    if (rem > mid || (rem == mid && (half & 1u))) ++half;
    return uint16_t(sign | half);
  }
  uint32_t half = ((absx - 0x38000000u) >> 13);
  uint32_t rem = absx & 0x1FFFu;
  if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ++half;
  return uint16_t(sign | half);
}

VV_HD F4 loadPx(const F4* frame, uint32_t i) { return frame[i]; }
VV_HD F4 loadPx(const H4* frame, uint32_t i) {
  H4 h = frame[i];
  return F4{halfToFloat(h.x), halfToFloat(h.y), halfToFloat(h.z), halfToFloat(h.w)};
}
VV_HD void storePx(F4* frame, uint32_t i, F4 v) { frame[i] = v; }
VV_HD void storePx(H4* frame, uint32_t i, F4 v) {
  frame[i] = H4{floatToHalf(v.x), floatToHalf(v.y), floatToHalf(v.z), floatToHalf(v.w)};
}

struct PlaneInfo {
  uint32_t width, height;
};
VV_HD int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

VV_HD bool isFiniteF(float v) { return v == v && v - v == 0.0f; }

// 2. [1 3 3 1]/8 separable downsample by 2 (single 4x4 pass), clamp-to-edge.
VV_HD void pxDownsample(const F4* src, F4* dst, PlaneInfo s, PlaneInfo d, uint32_t x, uint32_t y) {
  const float k[4] = {0.125f, 0.375f, 0.375f, 0.125f};
  F4 acc{0, 0, 0, 0};
  for (int ty = 0; ty < 4; ++ty) {
    int sy = clampi(int(y) * 2 - 1 + ty, 0, int(s.height) - 1);
    F4 row{0, 0, 0, 0};
    for (int tx = 0; tx < 4; ++tx) {
      int sx = clampi(int(x) * 2 - 1 + tx, 0, int(s.width) - 1);
      F4 v = src[sy * s.width + sx];
      row.x += k[tx] * v.x; row.y += k[tx] * v.y; row.z += k[tx] * v.z; row.w += k[tx] * v.w;
    }
    acc.x += k[ty] * row.x; acc.y += k[ty] * row.y; acc.z += k[ty] * row.z; acc.w += k[ty] * row.w;
  }
  dst[y * d.width + x] = acc;
}

// 3. Separable Gaussian, clamp-to-edge. `weights` has 2*radius+1 taps (core gaussianKernel()).
VV_HD void pxBlurH(const F4* src, F4* dst, PlaneInfo p, const float* weights, int radius, uint32_t x, uint32_t y) {
  F4 acc{0, 0, 0, 0};
  for (int i = -radius; i <= radius; ++i) {
    F4 v = src[y * p.width + clampi(int(x) + i, 0, int(p.width) - 1)];
    float k = weights[i + radius];
    acc.x += v.x * k; acc.y += v.y * k; acc.z += v.z * k; acc.w += v.w * k;
  }
  dst[y * p.width + x] = acc;
}
VV_HD void pxBlurV(const F4* src, F4* dst, PlaneInfo p, const float* weights, int radius, uint32_t x, uint32_t y) {
  F4 acc{0, 0, 0, 0};
  for (int i = -radius; i <= radius; ++i) {
    F4 v = src[clampi(int(y) + i, 0, int(p.height) - 1) * p.width + x];
    float k = weights[i + radius];
    acc.x += v.x * k; acc.y += v.y * k; acc.z += v.z * k; acc.w += v.w * k;
  }
  dst[y * p.width + x] = acc;
}

// 4. Bilinear 2x upsample (grid inverse of pxDownsample), weighted accumulate, clear.
VV_HD void pxUpsample(const F4* src, F4* dst, PlaneInfo s, PlaneInfo d, uint32_t x, uint32_t y) {
  float fy = vv_clamp((float(y) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.height - 1));
  float fx = vv_clamp((float(x) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.width - 1));
  int y0 = int(fy), x0 = int(fx);
  int y1 = y0 + 1 < int(s.height) ? y0 + 1 : int(s.height) - 1;
  int x1 = x0 + 1 < int(s.width) ? x0 + 1 : int(s.width) - 1;
  float ty = fy - float(y0), tx = fx - float(x0);
  F4 a = src[y0 * s.width + x0], b = src[y0 * s.width + x1], c = src[y1 * s.width + x0], e = src[y1 * s.width + x1];
  F4 top{a.x + (b.x - a.x) * tx, a.y + (b.y - a.y) * tx, a.z + (b.z - a.z) * tx, a.w + (b.w - a.w) * tx};
  F4 bot{c.x + (e.x - c.x) * tx, c.y + (e.y - c.y) * tx, c.z + (e.z - c.z) * tx, c.w + (e.w - c.w) * tx};
  dst[y * d.width + x] = F4{top.x + (bot.x - top.x) * ty, top.y + (bot.y - top.y) * ty, top.z + (bot.z - top.z) * ty,
                            top.w + (bot.w - top.w) * ty};
}
VV_HD void pxAxpy(F4* acc, const F4* v, float w, PlaneInfo p, uint32_t x, uint32_t y) {
  uint32_t i = y * p.width + x;
  acc[i].x += w * v[i].x; acc[i].y += w * v[i].y; acc[i].z += w * v[i].z; acc[i].w += w * v[i].w;
}
VV_HD void pxClear(F4* dst, PlaneInfo p, uint32_t x, uint32_t y) { dst[y * p.width + x] = F4{0, 0, 0, 0}; }

// Horizontal-only pyramid (vv::gaussianBlurHorizontal): [1 3 3 1]/8 downsample in x, linear
// upsample in x; rows are untouched.
VV_HD void pxDownsampleX(const F4* src, F4* dst, PlaneInfo s, PlaneInfo d, uint32_t x, uint32_t y) {
  const float k[4] = {0.125f, 0.375f, 0.375f, 0.125f};
  F4 acc{0, 0, 0, 0};
  for (int t = 0; t < 4; ++t) {
    F4 v = src[y * s.width + clampi(int(x) * 2 - 1 + t, 0, int(s.width) - 1)];
    acc.x += k[t] * v.x; acc.y += k[t] * v.y; acc.z += k[t] * v.z; acc.w += k[t] * v.w;
  }
  dst[y * d.width + x] = acc;
}
VV_HD void pxUpsampleX(const F4* src, F4* dst, PlaneInfo s, PlaneInfo d, uint32_t x, uint32_t y) {
  float fx = vv_clamp((float(x) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.width - 1));
  int x0 = int(fx);
  int x1 = x0 + 1 < int(s.width) ? x0 + 1 : int(s.width) - 1;
  float tx = fx - float(x0);
  F4 a = src[y * s.width + x0], b = src[y * s.width + x1];
  dst[y * d.width + x] = F4{a.x + (b.x - a.x) * tx, a.y + (b.y - a.y) * tx, a.z + (b.z - a.z) * tx, a.w + (b.w - a.w) * tx};
}


}  // namespace gpu
}  // namespace vv
