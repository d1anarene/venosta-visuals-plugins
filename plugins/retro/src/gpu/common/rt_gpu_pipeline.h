// Retro — GPU pass orchestration for any backend (CUDA, Metal, CPU emulation). The Backend provides
// the vv::gpu::Pyramid operations plus:
//   void rtPrepare(const void* src, bool half, Buf yuv, const FrameLayout&);
//   void rtComposite(const void* src, void* dst, bool half, Buf luma, Buf ring, Buf chroma,
//                    const FrameLayout&, const RtInfo&);
#pragma once
#include "gpu/vv_gpu_pyramid.h"
#include "rt.h"
#include "rt_gpu_pixels.h"

namespace rt {
namespace gpu {

template <typename Backend>
class Pipeline {
 public:
  using Buf = typename Backend::Buf;
  explicit Pipeline(Backend& b) : B(b), pyr(b) {}

  bool run(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, const Params& raw,
           int64_t frameKey, bool rgba, bool bottomUp) {
    if (w == 0 || h == 0) return false;
    const Params p = sanitized(raw);
    ensure(w, h);
    const FrameLayout f{w, h, rowPixels, rgba ? 1 : 0, bottomUp ? 1 : 0};
    B.rtPrepare(src, half, yuv, f);
    const Blurs b = blurs(p, int(h));
    Buf l = blurred(b.luma, luma), r = blurred(b.ring, ring), c = blurred(b.chroma, chroma);
    B.rtComposite(src, dst, half, l, r, c, f, info(p, int(w), int(h), frameKey));
    return true;
  }

 private:
  Backend& B;
  vv::gpu::Pyramid<Backend> pyr;
  uint32_t cachedW = 0, cachedH = 0;
  Buf yuv{}, luma{}, ring{}, chroma{};

  Buf blurred(float sigma, Buf out) {
    if (sigma < 0.05f) return yuv;  // no blur: sample the signal itself
    pyr.blurHorizontal(yuv, out, sigma);
    return out;
  }
  void ensure(uint32_t w, uint32_t h) {
    if (w == cachedW && h == cachedH) return;
    cachedW = w;
    cachedH = h;
    B.releaseAll();
    const size_t n = size_t(w) * h;
    yuv = B.alloc(n); luma = B.alloc(n); ring = B.alloc(n); chroma = B.alloc(n);
    pyr.allocate(w, h);
  }
};

}  // namespace gpu
}  // namespace rt
