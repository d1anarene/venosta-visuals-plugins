// Film Effects — film-specific per-pixel GPU passes (prepare, composite, Film Gate), shared by the
// CUDA kernels (src/gpu/cuda) and the CPU emulation used in tests. Valid C++17 and CUDA C++. The
// generic passes (pyramid, blur, half floats) come from the shared engine (gpu/vv_gpu_pixels.h).
//
// Each function computes ONE output pixel (x, y), exactly like one GPU thread. The CUDA kernels
// are thin wrappers that compute (x, y) from the thread index and call these; the emulation calls
// them in plain loops. The pass structure mirrors src/gpu/metal/FilmEffects.metal.
#pragma once
#include <cstdint>
#include <cstring>

#include "common/fe_kernels.h"
#include "gpu/vv_gpu_pixels.h"

#define FE_HD VV_HD

namespace fe {
namespace gpu {
using namespace vv::gpu;  // F4, H4, PlaneInfo, loadPx/storePx, pyramid and blur passes

struct FrameInfo {
  uint32_t width, height, rowPixels;
  int32_t space;
  float bloomThreshold, bloomKnee, halThreshold, halKnee;
  int32_t rgba;  // 1: host frame is RGBA (OpenFX), 0: BGRA (Premiere)
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
  int32_t rgba;     // 1: RGBA frame (OpenFX), 0: BGRA (Premiere)
  int32_t bottomUp; // 1: row 0 is the bottom row (OpenFX); grain uses top-down rows like the CPU path
};

/** Film Gate pass (see src/core/gate). Only 4-byte scalars and arrays of them, so the Metal mirror
 *  in FilmEffects.metal has the identical layout. */
struct GateInfo {
  uint32_t width, height, rowPixels;
  int32_t space, rgba, bottomUp;
  int32_t doWeave, doDefects, doEdge, look, scratchCount;
  float dx, dy, gain;
  float insetPx, softPx, radiusPx;
  float dustCellPx, dustDensity, dustSizePx;
  uint32_t dustKey;
  float mix;
  float sx0[4], sslope[4], swidth[4], sstrength[4];
  uint32_t skey[4];
  int32_t ssign[4];
};

// 1. Linearise the host frame (BGRA or RGBA) and extract bloom/halation sources.
template <typename T>
FE_HD void pxPrepare(const T* frame, F4* lin, F4* bright, F4* halSrc, const FrameInfo& fi, uint32_t x, uint32_t y) {
  F4 px = loadPx(frame, y * fi.rowPixels + x);
  vv_float3 l = vv_to_linear(fi.rgba ? vv_make3(px.x, px.y, px.z) : vv_make3(px.z, px.y, px.x), fi.space);
  uint32_t i = y * fi.width + x;
  lin[i] = F4{l.x, l.y, l.z, 0.0f};
  float w = vv_soft_knee_weight(vv_luma(l), fi.bloomThreshold, fi.bloomKnee);
  vv_float3 pos = vv_make3(vv_max(l.x, 0.0f), vv_max(l.y, 0.0f), vv_max(l.z, 0.0f));
  bright[i] = F4{pos.x * w, pos.y * w, pos.z * w, 0.0f};
  halSrc[i] = F4{fe_halation_source(pos, fi.halThreshold, fi.halKnee), 0.0f, 0.0f, 0.0f};
}

// 5. Composite: bloom + halation (linear) -> encoding -> grain -> mix; alpha untouched.
FE_HD float grainAt(uint32_t x, uint32_t y, const CompositeInfo& ci, int channel) {
  vv_uint key = vv_hash(ci.grainKey + vv_uint(channel) * 0x632BE5ABu);
  return fe_grain_field(float(x) + 0.5f, float(y) + 0.5f, ci.grainSizePx, ci.grainRoughness, key);
}


// Bloom + halation + grain for one pixel, without mix. `orig` is RGB in working encoding.
FE_HD void compositeColor(const float orig[3], const F4* lin, const F4* bloom, const F4* halNear, const F4* halFar,
                          const F4* halSrc, const CompositeInfo& ci, uint32_t x, uint32_t gy, uint32_t pi, float o[3]) {
  o[0] = orig[0]; o[1] = orig[1]; o[2] = orig[2];
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
      if (add[c] != 0.0f) o[c] = vv_from_linear1(l[c] + add[c], ci.space);
  }
  if (ci.doGrain) {
    float a = ci.grainAmp * fe_grain_response(vv_luma(vv_make3(o[0], o[1], o[2])), ci.grainShadows, ci.grainMids,
                                              ci.grainHighs);
    if (a > 0.0f) {
      float base = grainAt(x, gy, ci, 0);
      for (int c = 0; c < 3; ++c)
        o[c] += ci.grainColor ? a * (base + ci.grainChroma * grainAt(x, gy, ci, c + 1)) : a * base;
    }
  }
}

template <typename T>
FE_HD void pxComposite(const T* src, T* dst, const F4* lin, const F4* bloom, const F4* halNear, const F4* halFar,
                       const F4* halSrc, const CompositeInfo& ci, uint32_t x, uint32_t y) {
  const uint32_t fi = y * ci.rowPixels + x;
  const uint32_t pi = y * ci.width + x;
  const F4 s = loadPx(src, fi);
  const float orig[3] = {ci.rgba ? s.x : s.z, s.y, ci.rgba ? s.z : s.x};
  const uint32_t gy = ci.bottomUp ? ci.height - 1 - y : y;  // grain rows counted from the top
  float o[3] = {orig[0], orig[1], orig[2]};
  if (s.w > 0.0f) {
    compositeColor(orig, lin, bloom, halNear, halFar, halSrc, ci, x, gy, pi, o);
    for (int c = 0; c < 3; ++c) {
      float v = ci.mix < 1.0f ? orig[c] + (o[c] - orig[c]) * ci.mix : o[c];
      o[c] = isFiniteF(v) ? v : orig[c];
    }
  }
  if (ci.rgba) storePx(dst, fi, F4{o[0], o[1], o[2], s.w});  // alpha passed through
  else storePx(dst, fi, F4{o[2], o[1], o[0], s.w});          // RGB -> BGRA
}

// Film Gate, step 1: the finished film image (no mix) into `film` (RGB order, width stride, rows in
// the host frame's order).
template <typename T>
FE_HD void pxCompositeFilm(const T* src, F4* film, const F4* lin, const F4* bloom, const F4* halNear,
                           const F4* halFar, const F4* halSrc, const CompositeInfo& ci, uint32_t x, uint32_t y) {
  const uint32_t pi = y * ci.width + x;
  const F4 s = loadPx(src, y * ci.rowPixels + x);
  const float orig[3] = {ci.rgba ? s.x : s.z, s.y, ci.rgba ? s.z : s.x};
  float o[3] = {orig[0], orig[1], orig[2]};
  if (s.w > 0.0f) {
    compositeColor(orig, lin, bloom, halNear, halFar, halSrc, ci, x, ci.bottomUp ? ci.height - 1 - y : y, pi, o);
    for (int c = 0; c < 3; ++c) o[c] = isFiniteF(o[c]) ? o[c] : orig[c];
  }
  film[pi] = F4{o[0], o[1], o[2], s.w};
}

// Signed dust + scratch defect at film position (fx, fy), top row = 0.
FE_HD float gateDefectAt(const GateInfo& g, float fx, float fy) {
  float d = g.dustDensity > 0.0f ? fe_dust(fx, fy, g.dustCellPx, g.dustDensity, g.dustSizePx, g.dustKey, g.look) : 0.0f;
  for (int k = 0; k < g.scratchCount; ++k)
    d += float(g.ssign[k]) * fe_scratch(fx, fy, float(g.height), g.sx0[k], g.sslope[k], g.swidth[k], g.sstrength[k], g.skey[k]);
  return vv_clamp(d, -1.0f, 1.0f);
}

// Film Gate, step 2: weave (Catmull-Rom), dust/scratches, flicker, aperture, mix against the source.
template <typename T>
FE_HD void pxGate(const T* src, T* dst, const F4* film, const GateInfo& g, uint32_t x, uint32_t y) {
  const uint32_t fi = y * g.rowPixels + x;
  const F4 s = loadPx(src, fi);
  const float orig[3] = {g.rgba ? s.x : s.z, s.y, g.rgba ? s.z : s.x};
  float o[3] = {orig[0], orig[1], orig[2]};
  if (s.w > 0.0f) {
    const int H = int(g.height), W = int(g.width);
    const float ty = float(g.bottomUp ? g.height - 1 - y : y);  // top-down row
    const float fx = float(x) - g.dx, fy = ty - g.dy;              // film position seen here
    float col[3];
    if (g.doWeave) {
      const float bx = vv_floor(fx), by = vv_floor(fy);
      const float tx = fx - bx, tyf = fy - by;
      col[0] = col[1] = col[2] = 0.0f;
      for (int j = 0; j < 4; ++j) {
        int r = clampi(int(by) - 1 + j, 0, H - 1);
        if (g.bottomUp) r = H - 1 - r;  // top-down row -> buffer row
        const float wy = vv_catmull_rom(tyf, j);
        for (int i = 0; i < 4; ++i) {
          const int c = clampi(int(bx) - 1 + i, 0, W - 1);
          const float wgt = wy * vv_catmull_rom(tx, i);
          const F4 p = film[uint32_t(r) * g.width + uint32_t(c)];
          col[0] += wgt * p.x; col[1] += wgt * p.y; col[2] += wgt * p.z;
        }
      }
    } else {
      const F4 p = film[y * g.width + x];
      col[0] = p.x; col[1] = p.y; col[2] = p.z;
    }
    const float defect = g.doDefects ? gateDefectAt(g, fx, fy) : 0.0f;
    const float mask = g.doEdge ? fe_gate_mask(float(x), ty, float(g.width), float(g.height), g.insetPx, g.softPx, g.radiusPx) : 1.0f;
    const vv_float3 sh = fe_gate_shade(vv_make3(col[0], col[1], col[2]), defect, g.gain, mask, g.space);
    const float out[3] = {sh.x, sh.y, sh.z};
    for (int c = 0; c < 3; ++c) {
      float v = g.mix < 1.0f ? orig[c] + (out[c] - orig[c]) * g.mix : out[c];
      o[c] = isFiniteF(v) ? v : orig[c];
    }
  }
  if (g.rgba) storePx(dst, fi, F4{o[0], o[1], o[2], s.w});
  else storePx(dst, fi, F4{o[2], o[1], o[0], s.w});
}

}  // namespace gpu
}  // namespace fe
