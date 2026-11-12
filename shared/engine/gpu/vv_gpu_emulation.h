// Venosta Visuals engine — CPU emulation backend for the GPU pass functions, for tests: runs each
// pass as plain loops, one "thread" per pixel exactly like the CUDA kernels, so a plugin's GPU pass
// sequence can be compared with its CPU reference without a GPU. Buffers start as NaN so reading
// unwritten memory shows up. Plugins derive and add their own passes.
#pragma once
#include <cmath>
#include <deque>
#include <vector>

#include "gpu/vv_gpu_pixels.h"

namespace vv {
namespace gpu {

struct EmulationBackend {
  using Buf = F4*;
  std::deque<std::vector<F4>> store;

  void releaseAll() { store.clear(); }
  Buf alloc(size_t n) {
    store.emplace_back(n, F4{NAN, NAN, NAN, NAN});
    return store.back().data();
  }
  template <typename Fn>
  static void grid(uint32_t w, uint32_t h, Fn fn) {
    for (uint32_t y = 0; y < h; ++y)
      for (uint32_t x = 0; x < w; ++x) fn(x, y);
  }
  void downsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    grid(di.width, di.height, [&](uint32_t x, uint32_t y) { pxDownsample(s, d, si, di, x, y); });
  }
  void downsampleX(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    grid(di.width, di.height, [&](uint32_t x, uint32_t y) { pxDownsampleX(s, d, si, di, x, y); });
  }
  void blurH(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxBlurH(s, d, p, k.data(), int(k.size() / 2), x, y); });
  }
  void blurV(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxBlurV(s, d, p, k.data(), int(k.size() / 2), x, y); });
  }
  void upsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    grid(di.width, di.height, [&](uint32_t x, uint32_t y) { pxUpsample(s, d, si, di, x, y); });
  }
  void upsampleX(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    grid(di.width, di.height, [&](uint32_t x, uint32_t y) { pxUpsampleX(s, d, si, di, x, y); });
  }
  void axpy(Buf acc, Buf v, float w, PlaneInfo p) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxAxpy(acc, v, w, p, x, y); });
  }
  void clear(Buf d, PlaneInfo p) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxClear(d, p, x, y); });
  }
};

}  // namespace gpu
}  // namespace vv
