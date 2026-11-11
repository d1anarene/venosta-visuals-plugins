// Film Effects — Metal kernels. Mirrors the CPU reference pass by pass; the per-pixel maths is the
// shared header fe_kernels.h, which the CPU unit tests exercise.
//
// STATUS: NOT YET COMPILED (no Metal toolchain in the authoring environment).
// Build: xcrun -sdk macosx metal -fno-fast-math -I ../../core -c FilmEffects.metal -o FilmEffects.air
//        xcrun -sdk macosx metallib FilmEffects.air -o FilmEffects.metallib
// -fno-fast-math keeps pow/exp close to the CPU results (see docs/ARCHITECTURE.md, tolerances).
#include <metal_stdlib>
#include "common/fe_kernels.h"
using namespace metal;

struct FrameInfo {
  uint width;
  uint height;
  uint rowPixels;      // host frame row stride in pixels
  int space;           // FE_SPACE_*
  float bloomThreshold;
  float bloomKnee;
  float halThreshold;
  float halKnee;
  int rgba;            // 1: RGBA host frame (OpenFX), 0: BGRA (Premiere)
};

struct PlaneInfo {
  uint width;
  uint height;
};

struct BlurInfo {
  uint width;
  uint height;
  int radius;          // taps on each side; weights[0..2*radius]
};

struct CompositeInfo {
  uint width;
  uint height;
  uint rowPixels;
  int space;
  int doBloom;
  int doHal;
  int doGrain;
  float bloomIntensity;
  float halNearWeight;
  float halFarWeight;
  float halGain;         // intensity * 4 (same constant as the CPU path)
  float3 halTint;
  float grainAmp;        // amount * 0.12 * footprint
  float grainSizePx;
  float grainRoughness;
  float grainShadows;
  float grainMids;
  float grainHighs;
  int grainColor;
  float grainChroma;
  uint grainKey;         // per-frame key, channel keys derived like the CPU path
  float mix;
  int rgba;              // 1: RGBA host frame (OpenFX), 0: BGRA (Premiere)
  int bottomUp;          // 1: row 0 is the bottom row (OpenFX); grain rows count from the top
};

// ---------------------------------------------------------------------------------------------
// 1. Linearise the host frame (BGRA) and extract bloom/halation sources.
// ---------------------------------------------------------------------------------------------
template <typename T>
static void prepare_impl(device const T* frame, device float4* lin, device float4* bright, device float4* halSrc,
                         constant FrameInfo& fi, uint2 gid) {
  if (gid.x >= fi.width || gid.y >= fi.height) return;
  float4 px = float4(frame[gid.y * fi.rowPixels + gid.x]);
  float3 enc = fi.rgba != 0 ? px.xyz : float3(px.z, px.y, px.x);  // BGRA -> RGB
  float3 l = fe_to_linear(enc, fi.space);
  uint i = gid.y * fi.width + gid.x;
  lin[i] = float4(l, 0.0f);
  float w = fe_soft_knee_weight(fe_luma(l), fi.bloomThreshold, fi.bloomKnee);
  bright[i] = float4(max(l, float3(0.0f)) * w, 0.0f);
  float h = fe_halation_source(max(l, float3(0.0f)), fi.halThreshold, fi.halKnee);
  halSrc[i] = float4(h, 0.0f, 0.0f, 0.0f);
}

kernel void fe_prepare_f32(device const float4* frame [[buffer(0)]], device float4* lin [[buffer(1)]],
                           device float4* bright [[buffer(2)]], device float4* halSrc [[buffer(3)]],
                           constant FrameInfo& fi [[buffer(4)]], uint2 gid [[thread_position_in_grid]]) {
  prepare_impl(frame, lin, bright, halSrc, fi, gid);
}
kernel void fe_prepare_f16(device const half4* frame [[buffer(0)]], device float4* lin [[buffer(1)]],
                           device float4* bright [[buffer(2)]], device float4* halSrc [[buffer(3)]],
                           constant FrameInfo& fi [[buffer(4)]], uint2 gid [[thread_position_in_grid]]) {
  prepare_impl(frame, lin, bright, halSrc, fi, gid);
}

// ---------------------------------------------------------------------------------------------
// 2. Pyramid: [1 3 3 1]/8 separable downsample (single 4x4 pass), clamp-to-edge.
// ---------------------------------------------------------------------------------------------
kernel void fe_downsample(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                          constant PlaneInfo& s [[buffer(2)]], constant PlaneInfo& d [[buffer(3)]],
                          uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= d.width || gid.y >= d.height) return;
  const float k[4] = {0.125f, 0.375f, 0.375f, 0.125f};
  float4 acc = 0.0f;
  for (int ty = 0; ty < 4; ++ty) {
    int sy = clamp(int(gid.y) * 2 - 1 + ty, 0, int(s.height) - 1);
    float4 row = 0.0f;
    for (int tx = 0; tx < 4; ++tx) {
      int sx = clamp(int(gid.x) * 2 - 1 + tx, 0, int(s.width) - 1);
      row += k[tx] * src[sy * s.width + sx];
    }
    acc += k[ty] * row;
  }
  dst[gid.y * d.width + gid.x] = acc;
}

// ---------------------------------------------------------------------------------------------
// 3. Separable Gaussian, clamp-to-edge. Weights computed on the host exactly like the CPU kernel().
// ---------------------------------------------------------------------------------------------
kernel void fe_blur_h(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                      constant BlurInfo& b [[buffer(2)]], constant float* weights [[buffer(3)]],
                      uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= b.width || gid.y >= b.height) return;
  float4 acc = 0.0f;
  for (int i = -b.radius; i <= b.radius; ++i) {
    int x = clamp(int(gid.x) + i, 0, int(b.width) - 1);
    acc += src[gid.y * b.width + x] * weights[i + b.radius];
  }
  dst[gid.y * b.width + gid.x] = acc;
}
kernel void fe_blur_v(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                      constant BlurInfo& b [[buffer(2)]], constant float* weights [[buffer(3)]],
                      uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= b.width || gid.y >= b.height) return;
  float4 acc = 0.0f;
  for (int i = -b.radius; i <= b.radius; ++i) {
    int y = clamp(int(gid.y) + i, 0, int(b.height) - 1);
    acc += src[y * b.width + gid.x] * weights[i + b.radius];
  }
  dst[gid.y * b.width + gid.x] = acc;
}

// ---------------------------------------------------------------------------------------------
// 4. Bilinear 2x upsample (grid inverse of fe_downsample) and weighted accumulate.
// ---------------------------------------------------------------------------------------------
kernel void fe_upsample(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                        constant PlaneInfo& s [[buffer(2)]], constant PlaneInfo& d [[buffer(3)]],
                        uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= d.width || gid.y >= d.height) return;
  float fy = clamp((float(gid.y) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.height - 1));
  float fx = clamp((float(gid.x) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.width - 1));
  int y0 = int(fy), x0 = int(fx);
  int y1 = min(y0 + 1, int(s.height) - 1), x1 = min(x0 + 1, int(s.width) - 1);
  float ty = fy - float(y0), tx = fx - float(x0);
  float4 a = src[y0 * s.width + x0], bb = src[y0 * s.width + x1];
  float4 c = src[y1 * s.width + x0], e = src[y1 * s.width + x1];
  float4 top = a + (bb - a) * tx;
  float4 bot = c + (e - c) * tx;
  dst[gid.y * d.width + gid.x] = top + (bot - top) * ty;
}
kernel void fe_axpy(device float4* acc [[buffer(0)]], device const float4* x [[buffer(1)]],
                    constant float& weight [[buffer(2)]], constant PlaneInfo& p [[buffer(3)]],
                    uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= p.width || gid.y >= p.height) return;
  uint i = gid.y * p.width + gid.x;
  acc[i] += weight * x[i];
}
kernel void fe_clear(device float4* dst [[buffer(0)]], constant PlaneInfo& p [[buffer(1)]],
                     uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= p.width || gid.y >= p.height) return;
  dst[gid.y * p.width + gid.x] = 0.0f;
}

// ---------------------------------------------------------------------------------------------
// 5. Composite: bloom + halation (linear) -> encoding -> grain -> mix; alpha untouched.
// ---------------------------------------------------------------------------------------------
static float grain_sample(uint x, uint y, constant CompositeInfo& ci, int channel) {
  uint key = fe_hash(ci.grainKey + uint(channel) * 0x632BE5ABu);
  return fe_grain_field(float(x) + 0.5f, float(y) + 0.5f, ci.grainSizePx, ci.grainRoughness, key);
}

template <typename T>
static void composite_impl(device const T* src, device T* dst, device const float4* lin, device const float4* bloom,
                           device const float4* halNear, device const float4* halFar, device const float4* halSrc,
                           constant CompositeInfo& ci, uint2 gid) {
  if (gid.x >= ci.width || gid.y >= ci.height) return;
  uint fi = gid.y * ci.rowPixels + gid.x;
  uint pi = gid.y * ci.width + gid.x;
  float4 s = float4(src[fi]);
  float3 orig = ci.rgba != 0 ? s.xyz : float3(s.z, s.y, s.x);
  float alpha = s.w;
  uint gy = ci.bottomUp != 0 ? ci.height - 1 - gid.y : gid.y;  // grain rows counted from the top
  float3 o = orig;
  if (ci.doBloom != 0 || ci.doHal != 0) {
    float3 add = 0.0f;
    if (ci.doBloom != 0) add += bloom[pi].xyz * ci.bloomIntensity;
    if (ci.doHal != 0) {
      float blurred = ci.halNearWeight * halNear[pi].x + ci.halFarWeight * halFar[pi].x;
      add += fe_halation_spill(blurred, halSrc[pi].x) * ci.halGain * ci.halTint;
    }
    float3 l = lin[pi].xyz;
    o.x = add.x != 0.0f ? fe_from_linear1(l.x + add.x, ci.space) : orig.x;
    o.y = add.y != 0.0f ? fe_from_linear1(l.y + add.y, ci.space) : orig.y;
    o.z = add.z != 0.0f ? fe_from_linear1(l.z + add.z, ci.space) : orig.z;
  }
  if (ci.doGrain != 0) {
    float a = ci.grainAmp * fe_grain_response(fe_luma(o), ci.grainShadows, ci.grainMids, ci.grainHighs);
    if (a > 0.0f) {
      float base = grain_sample(gid.x, gy, ci, 0);
      if (ci.grainColor == 0) {
        o += a * base;
      } else {
        o.x += a * (base + ci.grainChroma * grain_sample(gid.x, gy, ci, 1));
        o.y += a * (base + ci.grainChroma * grain_sample(gid.x, gy, ci, 2));
        o.z += a * (base + ci.grainChroma * grain_sample(gid.x, gy, ci, 3));
      }
    }
  }
  if (alpha <= 0.0f) {
    o = orig;  // never invent colour where the layer is transparent
  } else if (ci.mix < 1.0f) {
    o = orig + (o - orig) * ci.mix;
  }
  if (!all(isfinite(o))) o = orig;
  dst[fi] = ci.rgba != 0 ? T(float4(o, alpha)) : T(float4(o.z, o.y, o.x, alpha));  // alpha passed through
}

kernel void fe_composite_f32(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                             device const float4* lin [[buffer(2)]], device const float4* bloom [[buffer(3)]],
                             device const float4* halNear [[buffer(4)]], device const float4* halFar [[buffer(5)]],
                             device const float4* halSrc [[buffer(6)]], constant CompositeInfo& ci [[buffer(7)]],
                             uint2 gid [[thread_position_in_grid]]) {
  composite_impl(src, dst, lin, bloom, halNear, halFar, halSrc, ci, gid);
}
kernel void fe_composite_f16(device const half4* src [[buffer(0)]], device half4* dst [[buffer(1)]],
                             device const float4* lin [[buffer(2)]], device const float4* bloom [[buffer(3)]],
                             device const float4* halNear [[buffer(4)]], device const float4* halFar [[buffer(5)]],
                             device const float4* halSrc [[buffer(6)]], constant CompositeInfo& ci [[buffer(7)]],
                             uint2 gid [[thread_position_in_grid]]) {
  composite_impl(src, dst, lin, bloom, halNear, halFar, halSrc, ci, gid);
}
