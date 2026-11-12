// Retro — per-pixel GPU passes (C++17 / CUDA) for the CUDA kernels and the CPU emulation test; the
// Metal shader mirrors them. Geometry and colour come from rt_kernels.h (shared with the CPU).
#pragma once
#include "gpu/vv_gpu_pixels.h"
#include "rt_kernels.h"

namespace rt {
namespace gpu {
using namespace vv::gpu;

struct FrameLayout {
  uint32_t width, height, rowPixels;
  int32_t rgba, bottomUp;
};

// 1. Host frame -> Y'UV (encoded), alpha in w.
template <typename T>
VV_HD void pxRtPrepare(const T* src, F4* yuv, const FrameLayout& f, uint32_t x, uint32_t y) {
  const F4 s = loadPx(src, y * f.rowPixels + x);
  const vv_float3 c = rt_rgb_to_yuv(f.rgba ? vv_make3(s.x, s.y, s.z) : vv_make3(s.z, s.y, s.x));
  yuv[y * f.width + x] = F4{c.x, c.y, c.z, s.w};
}

// Bilinear sample at (sx, sy) with sy counted from the top; same formula as the CPU reference.
VV_HD F4 rtSample(const F4* buf, const FrameLayout& f, float sx, float sy) {
  const float fx = vv_clamp(sx, 0.0f, float(f.width - 1)), fy = vv_clamp(sy, 0.0f, float(f.height - 1));
  const int x0 = int(fx), y0 = int(fy);
  const int x1 = x0 + 1 < int(f.width) ? x0 + 1 : int(f.width) - 1;
  const int y1 = y0 + 1 < int(f.height) ? y0 + 1 : int(f.height) - 1;
  const float tx = fx - float(x0), ty = fy - float(y0);
  const int r0 = f.bottomUp ? int(f.height) - 1 - y0 : y0, r1 = f.bottomUp ? int(f.height) - 1 - y1 : y1;
  const F4 a = buf[uint32_t(r0) * f.width + uint32_t(x0)], b = buf[uint32_t(r0) * f.width + uint32_t(x1)];
  const F4 c = buf[uint32_t(r1) * f.width + uint32_t(x0)], e = buf[uint32_t(r1) * f.width + uint32_t(x1)];
  F4 o;
  float top, bot;
  top = a.x + (b.x - a.x) * tx; bot = c.x + (e.x - c.x) * tx; o.x = top + (bot - top) * ty;
  top = a.y + (b.y - a.y) * tx; bot = c.y + (e.y - c.y) * tx; o.y = top + (bot - top) * ty;
  top = a.z + (b.z - a.z) * tx; bot = c.z + (e.z - c.z) * tx; o.z = top + (bot - top) * ty;
  o.w = 0.0f;
  return o;
}

// 2. Geometry, samples of the blurred signal, rt_finish, mix; alpha untouched.
template <typename T>
VV_HD void pxRtComposite(const T* src, T* dst, const F4* luma, const F4* ring, const F4* chroma, const FrameLayout& f,
                         const RtInfo& info, uint32_t x, uint32_t y) {
  const uint32_t i = y * f.rowPixels + x;
  const F4 s = loadPx(src, i);
  float o[3] = {f.rgba ? s.x : s.z, s.y, f.rgba ? s.z : s.x};
  if (s.w > 0.0f) {
    const float ty = float(f.bottomUp ? f.height - 1 - y : y);
    const vv_float3 g = rt_geometry(float(x), ty, info);
    const float ya = rtSample(luma, f, g.x, g.y).x, yr = rtSample(ring, f, g.x, g.y).x;
    const F4 cs = rtSample(chroma, f, g.x + info.chromaShiftPx, g.y);
    const vv_float3 c = rt_finish(ya, yr, cs.y, cs.z, vv_make3(o[0], o[1], o[2]), float(x), ty, g.x, g.y, g.z, info);
    const float cc[3] = {c.x, c.y, c.z};
    for (int k = 0; k < 3; ++k)
      if (isFiniteF(cc[k])) o[k] = cc[k];
  }
  if (f.rgba) storePx(dst, i, F4{o[0], o[1], o[2], s.w});
  else storePx(dst, i, F4{o[2], o[1], o[0], s.w});
}

}  // namespace gpu
}  // namespace rt
