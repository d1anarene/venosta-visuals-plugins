// Exposure & False Color — one GPU pass (C++17 / CUDA): host frame -> ef_shade -> host frame.
#pragma once
#include "ef_kernels.h"
#include "gpu/vv_gpu_pixels.h"

namespace ef {
namespace gpu {
using namespace vv::gpu;

struct FrameLayout {
  uint32_t rowPixels;
  int32_t rgba, bottomUp;
};

template <typename T>
VV_HD void pxEf(const T* src, T* dst, const EfInfo& info, const FrameLayout& f, uint32_t x, uint32_t y) {
  const uint32_t i = y * f.rowPixels + x;
  const F4 s = loadPx(src, i);
  float o[3] = {f.rgba ? s.x : s.z, s.y, f.rgba ? s.z : s.x};
  if (s.w > 0.0f) {
    const float ty = float(f.bottomUp ? info.height - 1 - y : y);  // geometry counts rows from the top
    const vv_float3 c = ef_shade(vv_make3(o[0], o[1], o[2]), float(x), ty, info);
    const float out[3] = {c.x, c.y, c.z};
    for (int k = 0; k < 3; ++k) {
      const float v = info.opacity >= 1.0f ? out[k] : o[k] + (out[k] - o[k]) * info.opacity;
      if (isFiniteF(v)) o[k] = v;
    }
  }
  if (f.rgba) storePx(dst, i, F4{o[0], o[1], o[2], s.w});
  else storePx(dst, i, F4{o[2], o[1], o[0], s.w});
}

}  // namespace gpu
}  // namespace ef
