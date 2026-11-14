// Denoise — Metal kernels of the spatial filter. Mirrors src/gpu/common/dn_gpu_pixels.h pass for
// pass (the CPU emulation test checks that sequence against the CPU reference; tests/gpu/
// dn_metal_vs_cpu.mm checks this shader on a Mac GPU). Generic pyramid kernels: vv_common.metal.
// Build: -fno-fast-math -I <repo>/shared/engine -I <repo>/shared/engine/core -I src/core
#include <metal_stdlib>
#include "gpu/metal/vv_common.metal"
#include "dn_color.h"
#include "dn_kernels.h"
using namespace metal;

// Must match dn::gpu::FrameLayout / BandInfo / vv::gpu::PlaneInfo (4-byte members).
struct FrameLayout { uint width, height, rowPixels; int rgba; int view; float mix; };
struct BandInfo { uint width, height; int band, hasScale; float tau, strength[3], recovery; };
struct Plane { uint width, height; };
struct Step { int step; };

template <typename T>
static void prepare_impl(device const T* src, device float4* opp, constant FrameLayout& f, uint2 g) {
  if (g.x >= f.width || g.y >= f.height) return;
  float4 s = float4(src[g.y * f.rowPixels + g.x]);
  float3 c = f.rgba != 0 ? s.xyz : float3(s.z, s.y, s.x);
  opp[g.y * f.width + g.x] = float4(dn_to_opponent(c), s.w);
}
kernel void dn_prepare_f32(device const float4* src [[buffer(0)]], device float4* opp [[buffer(1)]],
                           constant FrameLayout& f [[buffer(2)]], uint2 g [[thread_position_in_grid]]) { prepare_impl(src, opp, f, g); }
kernel void dn_prepare_f16(device const half4* src [[buffer(0)]], device float4* opp [[buffer(1)]],
                           constant FrameLayout& f [[buffer(2)]], uint2 g [[thread_position_in_grid]]) { prepare_impl(src, opp, f, g); }

kernel void dn_starlet_h(device const float4* s [[buffer(0)]], device float4* d [[buffer(1)]], constant Plane& p [[buffer(2)]],
                         constant Step& st [[buffer(3)]], uint2 g [[thread_position_in_grid]]) {
  if (g.x >= p.width || g.y >= p.height) return;
  const float k[5] = {1.0f / 16, 4.0f / 16, 6.0f / 16, 4.0f / 16, 1.0f / 16};
  float4 acc = float4(0.0f);
  for (int t = -2; t <= 2; ++t) acc += k[t + 2] * s[g.y * p.width + uint(clamp(int(g.x) + t * st.step, 0, int(p.width) - 1))];
  d[g.y * p.width + g.x] = acc;
}
kernel void dn_starlet_v_band(device const float4* tmp [[buffer(0)]], device const float4* cur [[buffer(1)]],
                              device float4* next [[buffer(2)]], device float4* band [[buffer(3)]],
                              device float4* energy [[buffer(4)]], device float4* var [[buffer(5)]],
                              constant DnProfileTable& t [[buffer(6)]], constant BandInfo& b [[buffer(7)]],
                              constant Step& st [[buffer(8)]], uint2 g [[thread_position_in_grid]]) {
  if (g.x >= b.width || g.y >= b.height) return;
  const float k[5] = {1.0f / 16, 4.0f / 16, 6.0f / 16, 4.0f / 16, 1.0f / 16};
  float4 acc = float4(0.0f);
  for (int s = -2; s <= 2; ++s) acc += k[s + 2] * tmp[uint(clamp(int(g.y) + s * st.step, 0, int(b.height) - 1)) * b.width + g.x];
  uint i = g.y * b.width + g.x;
  next[i] = acc;
  float3 d = cur[i].xyz - acc.xyz;
  band[i] = float4(d, 0.0f);
  energy[i] = float4(d * d, 0.0f);
  float sig[3 * DN_BINS];
  for (int q = 0; q < 3 * DN_BINS; ++q) sig[q] = t.sigma[b.band * 3 * DN_BINS + q];
  float cor[3] = {t.corr[b.band * 3], t.corr[b.band * 3 + 1], t.corr[b.band * 3 + 2]};
  var[i] = float4(dn_opponent_variance(sig, cor, dn_from_opponent(acc.xyz)), 0.0f);
}
kernel void dn_pilot(device const float4* band [[buffer(0)]], device const float4* E [[buffer(1)]], device const float4* var [[buffer(2)]],
                     device float4* p2 [[buffer(3)]], constant BandInfo& b [[buffer(4)]], uint2 g [[thread_position_in_grid]]) {
  if (g.x >= b.width || g.y >= b.height) return;
  uint i = g.y * b.width + g.x;
  p2[i] = float4(dn_pilot_sq(band[i].x, E[i].x, var[i].x, b.tau), dn_pilot_sq(band[i].y, E[i].y, var[i].y, b.tau),
                 dn_pilot_sq(band[i].z, E[i].z, var[i].z, b.tau), 0.0f);
}
kernel void dn_accumulate(device float4* out [[buffer(0)]], device const float4* band [[buffer(1)]], device const float4* P [[buffer(2)]],
                          device const float4* var [[buffer(3)]], constant BandInfo& b [[buffer(4)]], uint2 g [[thread_position_in_grid]]) {
  if (g.x >= b.width || g.y >= b.height) return;
  uint i = g.y * b.width + g.x;
  float rec = b.band == 0 ? b.recovery : 0.0f;
  float4 o = out[i];
  o.x += band[i].x * dn_wiener_gain(P[i].x, var[i].x, b.strength[0], rec);
  o.y += band[i].y * dn_wiener_gain(P[i].y, var[i].y, b.strength[1], rec);
  o.z += band[i].z * dn_wiener_gain(P[i].z, var[i].z, b.strength[2], rec);
  out[i] = o;
}
template <typename T>
static void finish_impl(device const T* src, device T* dst, device const float4* out, device const float4* res,
                        constant FrameLayout& f, uint2 g) {
  if (g.x >= f.width || g.y >= f.height) return;
  uint i = g.y * f.width + g.x, o = g.y * f.rowPixels + g.x;
  float4 s = float4(src[o]);
  float3 c = dn_from_opponent(out[i].xyz + res[i].xyz);
  float3 in = f.rgba != 0 ? s.xyz : float3(s.z, s.y, s.x);
  float3 v = f.view == 1 ? 0.5f + 4.0f * (in - c) : (f.view == 2 ? in : c);
  if (f.view == 0 && f.mix < 1.0f) v = in + (c - in) * f.mix;
  float3 r = in;
  if (isfinite(v.x)) r.x = v.x;
  if (isfinite(v.y)) r.y = v.y;
  if (isfinite(v.z)) r.z = v.z;
  dst[o] = f.rgba != 0 ? T(float4(r, s.w)) : T(float4(r.z, r.y, r.x, s.w));
}
kernel void dn_finish_f32(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]], device const float4* out [[buffer(2)]],
                          device const float4* res [[buffer(3)]], constant FrameLayout& f [[buffer(4)]], uint2 g [[thread_position_in_grid]]) {
  finish_impl(src, dst, out, res, f, g);
}
kernel void dn_finish_f16(device const half4* src [[buffer(0)]], device half4* dst [[buffer(1)]], device const float4* out [[buffer(2)]],
                          device const float4* res [[buffer(3)]], constant FrameLayout& f [[buffer(4)]], uint2 g [[thread_position_in_grid]]) {
  finish_impl(src, dst, out, res, f, g);
}
