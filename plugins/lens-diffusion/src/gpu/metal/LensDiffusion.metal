// Lens & Diffusion — Metal kernels. Mirrors src/gpu/common/ld_gpu_pixels.h pass for pass (the
// CPU emulation test checks that pass sequence against the CPU reference; tests/gpu/
// ld_metal_vs_cpu.mm checks this shader on a Mac GPU). Generic pyramid kernels: vv_common.metal.
// Build: -fno-fast-math -I <repo>/shared/engine -I <repo>/shared/engine/core -I src/core
#include <metal_stdlib>
#include "gpu/metal/vv_common.metal"
#include "ld_kernels.h"
using namespace metal;

// Must match ld::gpu::FrameInfo / AberrationInfo / WeightInfo / CompositeInfo (4-byte members).
struct FrameInfo { uint width, height, rowPixels; int space, rgba; };
struct AberrationInfo { uint width, height; float cornerPx, bias; };
struct WeightInfo { uint width, height; int doDiffusion, doStreaks; float bias, threshold, knee; };
struct CompositeInfo {
  uint width, height, rowPixels;
  int space, rgba;
  int doDiffusion, doStreaks, doVignette, vignetteStyle;
  float amount, diffTint[3];
  float intensity, streakTint[3];
  float vigAmount, vigSize, vigRoundness, vigFeather;
  float mix;
};

template <typename T>
static void prepare_impl(device const T* src, device float4* lin, constant FrameInfo& fi, uint2 gid) {
  if (gid.x >= fi.width || gid.y >= fi.height) return;
  float4 s = float4(src[gid.y * fi.rowPixels + gid.x]);
  float r = fi.rgba != 0 ? s.x : s.z, b = fi.rgba != 0 ? s.z : s.x;
  lin[gid.y * fi.width + gid.x] = float4(vv_to_linear1(r, fi.space), vv_to_linear1(s.y, fi.space), vv_to_linear1(b, fi.space), s.w);
}
kernel void ld_prepare_f32(device const float4* src [[buffer(0)]], device float4* lin [[buffer(1)]],
                           constant FrameInfo& fi [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
  prepare_impl(src, lin, fi, gid);
}
kernel void ld_prepare_f16(device const half4* src [[buffer(0)]], device float4* lin [[buffer(1)]],
                           constant FrameInfo& fi [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
  prepare_impl(src, lin, fi, gid);
}

static float ca_sample(device const float4* lin, uint w, uint h, float sx, float sy, int c) {
  float bx = floor(sx), by = floor(sy), tx = sx - bx, ty = sy - by;
  float acc = 0.0f;
  for (int j = 0; j < 4; ++j) {
    int yy = clamp(int(by) - 1 + j, 0, int(h) - 1);
    float wy = vv_catmull_rom(ty, j);
    for (int i = 0; i < 4; ++i) {
      int xx = clamp(int(bx) - 1 + i, 0, int(w) - 1);
      float4 v = lin[uint(yy) * w + uint(xx)];
      acc += wy * vv_catmull_rom(tx, i) * (c == 0 ? v.x : v.z);
    }
  }
  return acc;
}
kernel void ld_aberrate(device const float4* lin [[buffer(0)]], device float4* out [[buffer(1)]],
                        constant AberrationInfo& a [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= a.width || gid.y >= a.height) return;
  float cx = float(a.width) * 0.5f, cy = float(a.height) * 0.5f;
  float rc = sqrt(cx * cx + cy * cy);
  float vx = float(gid.x) + 0.5f - cx, vy = float(gid.y) + 0.5f - cy;
  float r = sqrt(vx * vx + vy * vy);
  float4 g = lin[gid.y * a.width + gid.x];
  float rb[2];
  for (int k = 0; k < 2; ++k) {
    float sgn = k == 0 ? 1.0f : -1.0f;
    float shift = sgn * ld_aberration_shift(r / rc, a.cornerPx, a.bias);
    float m = r > 1e-6f ? 1.0f - shift / r : 1.0f;
    rb[k] = ca_sample(lin, a.width, a.height, cx + vx * m - 0.5f, cy + vy * m - 0.5f, k == 0 ? 0 : 2);
  }
  out[gid.y * a.width + gid.x] = float4(rb[0], g.y, rb[1], g.w);
}

kernel void ld_weights(device const float4* lin [[buffer(0)]], device float4* s [[buffer(1)]],
                       device float4* b [[buffer(2)]], constant WeightInfo& wi [[buffer(3)]],
                       uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= wi.width || gid.y >= wi.height) return;
  uint i = gid.y * wi.width + gid.x;
  float3 cc = max(lin[i].xyz, float3(0.0f));
  float lum = vv_luma(cc);
  if (wi.doDiffusion != 0) s[i] = float4(cc * ld_diffusion_weight(lum, wi.bias), 0.0f);
  if (wi.doStreaks != 0) b[i] = float4(cc * vv_soft_knee_weight(lum, wi.threshold, wi.knee), 0.0f);
}

template <typename T>
static void composite_impl(device const T* src, device T* dst, device const float4* lin, device const float4* s,
                           device const float4* scat, device const float4* streak, constant CompositeInfo& ci, uint2 gid) {
  if (gid.x >= ci.width || gid.y >= ci.height) return;
  uint fi = gid.y * ci.rowPixels + gid.x, pi = gid.y * ci.width + gid.x;
  float4 sp = float4(src[fi]);
  float3 orig = ci.rgba != 0 ? sp.xyz : float3(sp.z, sp.y, sp.x);
  float3 o = orig;
  if (sp.w > 0.0f) {
    float3 l = lin[pi].xyz;
    if (ci.doDiffusion != 0) {
      float3 t = float3(ci.diffTint[0], ci.diffTint[1], ci.diffTint[2]);
      l += ci.amount * (scat[pi].xyz * t - s[pi].xyz);
    }
    if (ci.doStreaks != 0) {
      float3 t = float3(ci.streakTint[0], ci.streakTint[1], ci.streakTint[2]);
      l += ci.intensity * t * streak[pi].xyz;
    }
    float g = 1.0f;
    if (ci.doVignette != 0) {
      float d = ld_vignette_distance(float(gid.x), float(gid.y), float(ci.width), float(ci.height), ci.vigRoundness);
      g = ld_vignette_gain(d, ci.vigAmount, ci.vigSize, ci.vigFeather, ci.vignetteStyle);
    }
    for (int c = 0; c < 3; ++c) {
      float e = vv_from_linear1(l[c] * g, ci.space);
      float v = ci.mix >= 1.0f ? e : orig[c] + (e - orig[c]) * ci.mix;
      o[c] = isfinite(v) ? v : orig[c];
    }
  }
  dst[fi] = ci.rgba != 0 ? T(float4(o, sp.w)) : T(float4(o.z, o.y, o.x, sp.w));
}
kernel void ld_composite_f32(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                             device const float4* lin [[buffer(2)]], device const float4* s [[buffer(3)]],
                             device const float4* scat [[buffer(4)]], device const float4* streak [[buffer(5)]],
                             constant CompositeInfo& ci [[buffer(6)]], uint2 gid [[thread_position_in_grid]]) {
  composite_impl(src, dst, lin, s, scat, streak, ci, gid);
}
kernel void ld_composite_f16(device const half4* src [[buffer(0)]], device half4* dst [[buffer(1)]],
                             device const float4* lin [[buffer(2)]], device const float4* s [[buffer(3)]],
                             device const float4* scat [[buffer(4)]], device const float4* streak [[buffer(5)]],
                             constant CompositeInfo& ci [[buffer(6)]], uint2 gid [[thread_position_in_grid]]) {
  composite_impl(src, dst, lin, s, scat, streak, ci, gid);
}
