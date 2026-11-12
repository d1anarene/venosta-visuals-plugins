// Venosta Visuals engine — generic Metal kernels (pyramid, separable Gaussian, x-only pyramid,
// accumulate, clear), the Metal counterparts of gpu/vv_gpu_pixels.h. #include this file from a
// plugin's .metal source (compile with -I <repo>/shared/engine -I <repo>/shared/engine/core).
// Used through vv::gpu::MetalBackend (metal/vv_metal_backend.h).
#ifndef VV_COMMON_METAL
#define VV_COMMON_METAL
#include <metal_stdlib>
using namespace metal;

struct PlaneInfo {
  uint width;
  uint height;
};
struct BlurInfo {
  uint width;
  uint height;
  int radius;  // taps on each side; weights[0..2*radius]
};

kernel void vv_downsample(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
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
kernel void vv_downsample_x(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                            constant PlaneInfo& s [[buffer(2)]], constant PlaneInfo& d [[buffer(3)]],
                            uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= d.width || gid.y >= d.height) return;
  const float k[4] = {0.125f, 0.375f, 0.375f, 0.125f};
  float4 acc = 0.0f;
  for (int t = 0; t < 4; ++t) acc += k[t] * src[gid.y * s.width + clamp(int(gid.x) * 2 - 1 + t, 0, int(s.width) - 1)];
  dst[gid.y * d.width + gid.x] = acc;
}
kernel void vv_blur_h(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                      constant BlurInfo& b [[buffer(2)]], constant float* weights [[buffer(3)]],
                      uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= b.width || gid.y >= b.height) return;
  float4 acc = 0.0f;
  for (int i = -b.radius; i <= b.radius; ++i)
    acc += src[gid.y * b.width + clamp(int(gid.x) + i, 0, int(b.width) - 1)] * weights[i + b.radius];
  dst[gid.y * b.width + gid.x] = acc;
}
kernel void vv_blur_v(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                      constant BlurInfo& b [[buffer(2)]], constant float* weights [[buffer(3)]],
                      uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= b.width || gid.y >= b.height) return;
  float4 acc = 0.0f;
  for (int i = -b.radius; i <= b.radius; ++i)
    acc += src[clamp(int(gid.y) + i, 0, int(b.height) - 1) * b.width + gid.x] * weights[i + b.radius];
  dst[gid.y * b.width + gid.x] = acc;
}
kernel void vv_upsample(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
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
kernel void vv_upsample_x(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
                          constant PlaneInfo& s [[buffer(2)]], constant PlaneInfo& d [[buffer(3)]],
                          uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= d.width || gid.y >= d.height) return;
  float fx = clamp((float(gid.x) + 0.5f) * 0.5f - 0.5f, 0.0f, float(s.width - 1));
  int x0 = int(fx), x1 = min(x0 + 1, int(s.width) - 1);
  float tx = fx - float(x0);
  float4 a = src[gid.y * s.width + x0], b = src[gid.y * s.width + x1];
  dst[gid.y * d.width + gid.x] = a + (b - a) * tx;
}
kernel void vv_axpy(device float4* acc [[buffer(0)]], device const float4* x [[buffer(1)]],
                    constant float& weight [[buffer(2)]], constant PlaneInfo& p [[buffer(3)]],
                    uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= p.width || gid.y >= p.height) return;
  uint i = gid.y * p.width + gid.x;
  acc[i] += weight * x[i];
}
kernel void vv_clear(device float4* dst [[buffer(0)]], constant PlaneInfo& p [[buffer(1)]],
                     uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= p.width || gid.y >= p.height) return;
  dst[gid.y * p.width + gid.x] = 0.0f;
}
#endif  // VV_COMMON_METAL
