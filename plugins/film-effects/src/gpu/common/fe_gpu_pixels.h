// Film Effects — per-pixel GPU pass functions, shared by the CUDA kernels (src/gpu/cuda) and the
// CPU emulation used in tests (tests/gpu/gpu_emulation_test.cpp). Valid C++17 and CUDA C++.
//
// Each function computes ONE output pixel (x, y), exactly like one GPU thread. The CUDA kernels
// are thin wrappers that compute (x, y) from the thread index and call these; the emulation calls
// them in plain loops. The pass structure mirrors src/gpu/metal/FilmEffects.metal.
#pragma once
#include <cstdint>
#include <cstring>

#include "common/fe_kernels.h"

#ifdef __CUDACC__
#define FE_HD __host__ __device__ inline
#else
#define FE_HD inline
#endif

namespace fe {
namespace gpu {

struct alignas(16) F4 {
  float x, y, z, w;
};

/** Half-float pixel (Premiere GPU_BGRA_4444_16f): four IEEE binary16 values. */
struct alignas(8) H4 {
  uint16_t x, y, z, w;
};

// Portable IEEE half <-> float (round to nearest even), identical on host and device.
FE_HD float halfToFloat(uint16_t h) {
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

FE_HD uint16_t floatToHalf(float f) {
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

FE_HD F4 loadPx(const F4* frame, uint32_t i) { return frame[i]; }
FE_HD F4 loadPx(const H4* frame, uint32_t i) {
  H4 h = frame[i];
  return F4{halfToFloat(h.x), halfToFloat(h.y), halfToFloat(h.z), halfToFloat(h.w)};
}
FE_HD void storePx(F4* frame, uint32_t i, F4 v) { frame[i] = v; }
FE_HD void storePx(H4* frame, uint32_t i, F4 v) {
  frame[i] = H4{floatToHalf(v.x), floatToHalf(v.y), floatToHalf(v.z), floatToHalf(v.w)};
}

struct FrameInfo {
  uint32_t width, height, rowPixels;
  int32_t space;
  float bloomThreshold, bloomKnee, halThreshold, halKnee;
};
struct PlaneInfo {
  uint32_t width, height;
};
struct CompositeInfo {
  uint32_t width, height, rowPixels;
  int32_t space, doBloom, doHal, doGrain;
  float bloomIntensity, halNearWeight, halFarWeight, halGain;
  float halTint[3];
  float grainAmp, grainSizePx, grainRoughness, grainShadows, grainMids, grainHighs;
  int32_t grainColor;
  float grainChroma;
  uint32_t grainKey;
  float mix;
};

FE_HD int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 1. Linearise the host frame (BGRA) and extract bloom/halation sources.
template <typename T>
FE_HD void pxPrepare(const T* frame, F4* lin, F4* bright, F4* halSrc, const FrameInfo& fi, uint32_t x, uint32_t y) {
  F4 px = loadPx(frame, y * fi.rowPixels + x);
  fe_float3 l = fe_to_linear(fe_make3(px.z, px.y, px.x), fi.space);  // BGRA -> RGB
  uint32_t i = y * fi.width + x;
  lin[i] = F4{l.x, l.y, l.z, 0.0f};
  float w = fe_soft_knee_weight(fe_luma(l), fi.bloomThreshold, fi.bloomKnee);
  fe_float3 pos = fe_make3(fe_max(l.x, 0.0f), fe_max(l.y, 0.0f), fe_max(l.z, 0.0f));
  bright[i] = F4{pos.x * w, pos.y * w, pos.z * w, 0.0f};
  halSrc[i] = F4{fe_halation_source(pos, fi.halThreshold, fi.halKnee), 0.0f, 0.0f, 0.0f};
}

// 2. [1 3 3 1]/8 separable downsample by 2 (single 4x4 pass), clamp-to-edge.
FE_HD void pxDownsample(const F4* src, F4* dst, PlaneInfo s, PlaneInfo d, uint32_t x, uint32_t y) {
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
FE_HD void pxBlurH(const F4* src, F4* dst, PlaneInfo p, const float* weights, int radius, uint32_t x, uint32_t y) {
  F4 acc{0, 0, 0, 0};
  for (int i = -radius; i <= radius; ++i) {
    F4 v = src[y * p.width + clampi(int(x) + i, 0, int(p.width) - 1)];
    float k = weights[i + radius];
    acc.x += v.x * k; acc.y += v.y * k; acc.z += v.z * k; acc.w += v.w * k;
  }
  dst[y * p.width + x] = acc;
}
FE_HD void pxBlurV(const F4* src, F4* dst, PlaneInfo p, const float* weights, int radius, uint32_t x, uint32_t y) {
  F4 acc{0, 0, 0, 0};
  for (int i = -radius; i <= radius; ++i) {
    F4 v = src[clampi(int(y) + i, 0, int(p.height) - 1) * p.width + x];
    float k = weights[i + radius];
    acc.x += v.x * k; acc.y += v.y * k; acc.z += v.z * k; acc.w += v.w * k;
  }
  dst[y * p.width + x] = acc;
}

// 4. Bilinear 2x upsample (grid inverse of pxDownsample), weighted accumulate, clear.
FE_HD void pxUpsample(const F4* src, F4* dst, PlaneInfo s, PlaneInfo d, uint32_t x, uint32_t y) {
  float fy = fe_clamp((float(y) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.height - 1));
  float fx = fe_clamp((float(x) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.width - 1));
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
FE_HD void pxAxpy(F4* acc, const F4* v, float w, PlaneInfo p, uint32_t x, uint32_t y) {
  uint32_t i = y * p.width + x;
  acc[i].x += w * v[i].x; acc[i].y += w * v[i].y; acc[i].z += w * v[i].z; acc[i].w += w * v[i].w;
}
FE_HD void pxClear(F4* dst, PlaneInfo p, uint32_t x, uint32_t y) { dst[y * p.width + x] = F4{0, 0, 0, 0}; }

// 5. Composite: bloom + halation (linear) -> encoding -> grain -> mix; alpha untouched.
FE_HD float grainAt(uint32_t x, uint32_t y, const CompositeInfo& ci, int channel) {
  fe_uint key = fe_hash(ci.grainKey + fe_uint(channel) * 0x632BE5ABu);
  return fe_grain_field(float(x) + 0.5f, float(y) + 0.5f, ci.grainSizePx, ci.grainRoughness, key);
}

FE_HD bool isFiniteF(float v) { return v == v && v - v == 0.0f; }

template <typename T>
FE_HD void pxComposite(const T* src, T* dst, const F4* lin, const F4* bloom, const F4* halNear, const F4* halFar,
                       const F4* halSrc, const CompositeInfo& ci, uint32_t x, uint32_t y) {
  const uint32_t fi = y * ci.rowPixels + x;
  const uint32_t pi = y * ci.width + x;
  const F4 s = loadPx(src, fi);
  const float orig[3] = {s.z, s.y, s.x};  // BGRA -> RGB
  float o[3] = {orig[0], orig[1], orig[2]};
  if (s.w > 0.0f) {
    if (ci.doBloom || ci.doHal) {
      float add[3] = {0, 0, 0};
      if (ci.doBloom) {
        add[0] += bloom[pi].x * ci.bloomIntensity;
        add[1] += bloom[pi].y * ci.bloomIntensity;
        add[2] += bloom[pi].z * ci.bloomIntensity;
      }
      if (ci.doHal) {
        float blurred = ci.halNearWeight * halNear[pi].x + ci.halFarWeight * halFar[pi].x;
        float spill = fe_halation_spill(blurred, halSrc[pi].x) * ci.halGain;
        for (int c = 0; c < 3; ++c) add[c] += spill * ci.halTint[c];
      }
      const float l[3] = {lin[pi].x, lin[pi].y, lin[pi].z};
      for (int c = 0; c < 3; ++c)
        if (add[c] != 0.0f) o[c] = fe_from_linear1(l[c] + add[c], ci.space);
    }
    if (ci.doGrain) {
      float a = ci.grainAmp * fe_grain_response(fe_luma(fe_make3(o[0], o[1], o[2])), ci.grainShadows, ci.grainMids,
                                                ci.grainHighs);
      if (a > 0.0f) {
        float base = grainAt(x, y, ci, 0);
        for (int c = 0; c < 3; ++c)
          o[c] += ci.grainColor ? a * (base + ci.grainChroma * grainAt(x, y, ci, c + 1)) : a * base;
      }
    }
    for (int c = 0; c < 3; ++c) {
      float v = ci.mix < 1.0f ? orig[c] + (o[c] - orig[c]) * ci.mix : o[c];
      o[c] = isFiniteF(v) ? v : orig[c];
    }
  }
  storePx(dst, fi, F4{o[2], o[1], o[0], s.w});  // RGB -> BGRA, alpha passed through
}

}  // namespace gpu
}  // namespace fe
