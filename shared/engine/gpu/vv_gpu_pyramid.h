// Venosta Visuals engine — GPU Gaussian pyramid for any backend (CUDA, CPU emulation; Metal has its
// own equivalent in metal/). Makes exactly the decisions of the CPU reference (vv/blur.h,
// vv/blur_plan.h), so GPU and CPU blurs agree.
//
// A Backend provides:
//   using Buf = ...;                          // device buffer of F4 elements
//   Buf alloc(size_t elements);
//   void downsample(Buf src, Buf dst, PlaneInfo s, PlaneInfo d);
//   void blurH(Buf src, Buf dst, PlaneInfo p, const std::vector<float>& weights);
//   void blurV(Buf src, Buf dst, PlaneInfo p, const std::vector<float>& weights);
//   void upsample(Buf src, Buf dst, PlaneInfo s, PlaneInfo d);
//   void axpy(Buf acc, Buf x, float w, PlaneInfo p);
//   void clear(Buf dst, PlaneInfo p);
// Every call must complete before the next one starts reading its output (in-order stream).
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "gpu/vv_gpu_pixels.h"
#include "vv/blur.h"
#include "vv/blur_plan.h"

namespace vv {
namespace gpu {

inline PlaneInfo halfPlane(PlaneInfo p) { return {std::max(1u, (p.width + 1) / 2), std::max(1u, (p.height + 1) / 2)}; }

template <typename Backend>
class Pyramid {
 public:
  using Buf = typename Backend::Buf;
  explicit Pyramid(Backend& b) : B(b) {}

  /** Allocate the levels for a w x h frame (level 0 = full resolution). The caller releases the
   *  backend's previous buffers first when the frame size changes. */
  void allocate(uint32_t w, uint32_t h) {
    dims.clear();
    levels.clear(); accA.clear(); accB.clear(); tmpA.clear(); tmpB.clear();
    PlaneInfo p{w, h};
    for (int L = 0; L < 14; ++L) {  // enough levels for any frame size
      dims.push_back(p);
      const size_t n = size_t(p.width) * p.height;
      levels.push_back(B.alloc(n));
      accA.push_back(B.alloc(n));
      accB.push_back(B.alloc(n));
      tmpA.push_back(B.alloc(n));
      tmpB.push_back(B.alloc(n));
      if (p.width == 1 && p.height == 1) break;
      p = halfPlane(p);
    }
  }

  /** Separable Gaussian src -> dst via tmp; sigma < 0.05 copies (same rule as the CPU core). */
  void blur(Buf src, Buf tmp, Buf dst, PlaneInfo p, float sigma) {
    if (sigma < 0.05f) {
      B.clear(dst, p);
      B.axpy(dst, src, 1.0f, p);
      return;
    }
    const std::vector<float> k = gaussianKernel(sigma);
    B.blurH(src, tmp, p, k);
    B.blurV(tmp, dst, p, k);
  }

  /** Equivalent of vv::blurByPlan: downsample `level` times, blur, upsample back into `out`.
   *  Uses tmpA/tmpB/accA of the levels >= 1 as scratch. */
  void blurByPlan(Buf src, Buf out, const BlurStep& step) {
    Buf cur = src;
    for (int L = 1; L <= step.level; ++L) {
      B.downsample(cur, tmpA[L], dims[L - 1], dims[L]);
      cur = tmpA[L];
    }
    Buf blurred = step.level == 0 ? out : accA[step.level];
    blur(cur, tmpB[step.level], blurred, dims[step.level], step.sigmaAtLevel);
    for (int L = step.level - 1; L >= 0; --L) {
      Buf d = L == 0 ? out : accA[L];
      B.upsample(blurred, d, dims[L + 1], dims[L]);
      blurred = d;
    }
  }

  std::vector<Buf> levels, accA, accB, tmpA, tmpB;  // per level
  std::vector<PlaneInfo> dims;

 private:
  Backend& B;
};

}  // namespace gpu
}  // namespace vv
