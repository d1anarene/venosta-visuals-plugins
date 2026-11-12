// Exposure & False Color — Metal kernel: the shared per-pixel function ef_shade (ef_kernels.h).
// Build: -fno-fast-math -I <repo>/shared/engine -I <repo>/shared/engine/core -I src/core
#include <metal_stdlib>
#include "gpu/metal/vv_common.metal"
#include "ef_kernels.h"
using namespace metal;

struct FrameLayout { uint rowPixels; int rgba, bottomUp; };

template <typename T>
static void ef_impl(device const T* src, device T* dst, constant EfInfo& info, constant FrameLayout& f, uint2 gid) {
  if (gid.x >= info.width || gid.y >= info.height) return;
  uint i = gid.y * f.rowPixels + gid.x;
  float4 s = float4(src[i]);
  float3 o = f.rgba != 0 ? s.xyz : float3(s.z, s.y, s.x);
  if (s.w > 0.0f) {
    EfInfo inf = info;
    float ty = float(f.bottomUp != 0 ? info.height - 1 - gid.y : gid.y);
    float3 c = ef_shade(o, float(gid.x), ty, inf);
    float3 v = inf.opacity >= 1.0f ? c : o + (c - o) * inf.opacity;
    if (isfinite(v.x)) o.x = v.x;
    if (isfinite(v.y)) o.y = v.y;
    if (isfinite(v.z)) o.z = v.z;
  }
  dst[i] = f.rgba != 0 ? T(float4(o, s.w)) : T(float4(o.z, o.y, o.x, s.w));
}
kernel void ef_f32(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                   constant EfInfo& info [[buffer(2)]], constant FrameLayout& f [[buffer(3)]],
                   uint2 gid [[thread_position_in_grid]]) {
  ef_impl(src, dst, info, f, gid);
}
kernel void ef_f16(device const half4* src [[buffer(0)]], device half4* dst [[buffer(1)]],
                   constant EfInfo& info [[buffer(2)]], constant FrameLayout& f [[buffer(3)]],
                   uint2 gid [[thread_position_in_grid]]) {
  ef_impl(src, dst, info, f, gid);
}
