// Retro — Metal kernels. Mirrors src/gpu/common/rt_gpu_pixels.h (the CPU emulation test checks that
// pass sequence against the CPU reference; tests/gpu/rt_metal_vs_cpu.mm checks this shader on a Mac
// GPU). Generic pyramid kernels: vv_common.metal.
// Build: -fno-fast-math -I <repo>/shared/engine -I <repo>/shared/engine/core -I src/core
#include <metal_stdlib>
#include "gpu/metal/vv_common.metal"
#include "rt_kernels.h"
using namespace metal;

// Must match rt::gpu::FrameLayout (4-byte members).
struct FrameLayout { uint width, height, rowPixels; int rgba, bottomUp; };

template <typename T>
static void prepare_impl(device const T* src, device float4* yuv, constant FrameLayout& f, uint2 gid) {
  if (gid.x >= f.width || gid.y >= f.height) return;
  float4 s = float4(src[gid.y * f.rowPixels + gid.x]);
  float3 c = rt_rgb_to_yuv(f.rgba != 0 ? s.xyz : float3(s.z, s.y, s.x));
  yuv[gid.y * f.width + gid.x] = float4(c, s.w);
}
kernel void rt_prepare_f32(device const float4* src [[buffer(0)]], device float4* yuv [[buffer(1)]],
                           constant FrameLayout& f [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
  prepare_impl(src, yuv, f, gid);
}
kernel void rt_prepare_f16(device const half4* src [[buffer(0)]], device float4* yuv [[buffer(1)]],
                           constant FrameLayout& f [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
  prepare_impl(src, yuv, f, gid);
}

static float3 rt_sample(device const float4* buf, constant FrameLayout& f, float sx, float sy) {
  float fx = clamp(sx, 0.0f, float(f.width - 1)), fy = clamp(sy, 0.0f, float(f.height - 1));
  int x0 = int(fx), y0 = int(fy);
  int x1 = min(x0 + 1, int(f.width) - 1), y1 = min(y0 + 1, int(f.height) - 1);
  float tx = fx - float(x0), ty = fy - float(y0);
  int r0 = f.bottomUp != 0 ? int(f.height) - 1 - y0 : y0, r1 = f.bottomUp != 0 ? int(f.height) - 1 - y1 : y1;
  float3 a = buf[uint(r0) * f.width + uint(x0)].xyz, b = buf[uint(r0) * f.width + uint(x1)].xyz;
  float3 c = buf[uint(r1) * f.width + uint(x0)].xyz, e = buf[uint(r1) * f.width + uint(x1)].xyz;
  float3 top = a + (b - a) * tx, bot = c + (e - c) * tx;
  return top + (bot - top) * ty;
}

template <typename T>
static void composite_impl(device const T* src, device T* dst, device const float4* luma, device const float4* ring,
                           device const float4* chroma, constant FrameLayout& f, constant RtInfo& info, uint2 gid) {
  if (gid.x >= f.width || gid.y >= f.height) return;
  uint i = gid.y * f.rowPixels + gid.x;
  float4 s = float4(src[i]);
  float3 o = f.rgba != 0 ? s.xyz : float3(s.z, s.y, s.x);
  if (s.w > 0.0f) {
    RtInfo inf = info;
    float ty = float(f.bottomUp != 0 ? f.height - 1 - gid.y : gid.y);
    float3 g = rt_geometry(float(gid.x), ty, inf);
    float ya = rt_sample(luma, f, g.x, g.y).x, yr = rt_sample(ring, f, g.x, g.y).x;
    float3 cs = rt_sample(chroma, f, g.x + inf.chromaShiftPx, g.y);
    float3 c = rt_finish(ya, yr, cs.y, cs.z, o, float(gid.x), ty, g.x, g.y, g.z, inf);
    if (isfinite(c.x)) o.x = c.x;
    if (isfinite(c.y)) o.y = c.y;
    if (isfinite(c.z)) o.z = c.z;
  }
  dst[i] = f.rgba != 0 ? T(float4(o, s.w)) : T(float4(o.z, o.y, o.x, s.w));
}
kernel void rt_composite_f32(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                             device const float4* luma [[buffer(2)]], device const float4* ring [[buffer(3)]],
                             device const float4* chroma [[buffer(4)]], constant FrameLayout& f [[buffer(5)]],
                             constant RtInfo& info [[buffer(6)]], uint2 gid [[thread_position_in_grid]]) {
  composite_impl(src, dst, luma, ring, chroma, f, info, gid);
}
kernel void rt_composite_f16(device const half4* src [[buffer(0)]], device half4* dst [[buffer(1)]],
                             device const float4* luma [[buffer(2)]], device const float4* ring [[buffer(3)]],
                             device const float4* chroma [[buffer(4)]], constant FrameLayout& f [[buffer(5)]],
                             constant RtInfo& info [[buffer(6)]], uint2 gid [[thread_position_in_grid]]) {
  composite_impl(src, dst, luma, ring, chroma, f, info, gid);
}
