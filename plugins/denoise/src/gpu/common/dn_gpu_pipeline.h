// Denoise — spatial filter as GPU passes for any backend (CUDA, Metal, CPU emulation). Same steps
// and blur plans as dn_spatial.cpp. The Backend provides the vv::gpu::Pyramid operations plus:
//   void dnPrepare(const void* src, bool half, Buf opp, const FrameLayout&);
//   void dnStarletH(Buf src, Buf tmp, PlaneInfo, int step);
//   void dnStarletVBand(Buf tmp, Buf cur, Buf next, Buf band, Buf energy, Buf var, const DnProfileTable&,
//                       const BandInfo&, int step);
//   void dnPilot(Buf band, Buf E, Buf var, Buf pilot2, const BandInfo&);
//   void dnAccumulate(Buf out, Buf band, Buf P, Buf var, const BandInfo&);
//   void dnFinish(const void* src, void* dst, bool half, Buf out, Buf residual, const FrameLayout&);
// Per band: 4 full-frame passes + 2 pyramid blurs. Memory (F4 full-res buffers): 9 + the pyramid's scratch. Rows may be bottom-up (OFX): every
// pass is symmetric in y, so results do not depend on row order.
#pragma once
#include "dn_gpu_pixels.h"
#include "dn_profile.h"
#include "dn_spatial.h"
#include "gpu/vv_gpu_pyramid.h"
#include "vv/blur_plan.h"

namespace dn {
namespace gpu {

inline DnProfileTable toTable(const NoiseProfile& p) {
  DnProfileTable t{};
  for (int b = 0; b < DN_BANDS; ++b)
    for (int c = 0; c < 3; ++c) {
      for (int k = 0; k < DN_BINS; ++k) t.sigma[(b * 3 + c) * DN_BINS + k] = p.sigma[b][c][k];
      t.corr[b * 3 + c] = p.corr[b][c];
    }
  return t;
}

template <typename Backend>
class SpatialPipeline {
 public:
  using Buf = typename Backend::Buf;
  explicit SpatialPipeline(Backend& b) : B(b), pyr(b) {}

  bool run(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, const NoiseProfile& prof,
           const SpatialParams& sp, bool rgba, int view = 0, float mix = 1.0f) {
    if (w == 0 || h == 0) return false;
    ensure(w, h);
    const PlaneInfo p{w, h};
    const FrameLayout f{w, h, rowPixels, rgba ? 1 : 0, view, mix};
    const DnProfileTable table = toTable(prof);
    B.dnPrepare(src, half, oppA, f);
    B.clear(out, p);
    Buf cur = oppA, next = oppB;
    for (int j = 0; j < DN_BANDS; ++j) {
      const BandInfo bi{w, h, j, 0, sp.pilotTau, {sp.lumaStrength, sp.chromaStrength, sp.chromaStrength}, sp.detailRecovery};
      B.dnStarletH(cur, tmp, p, 1 << j);
      B.dnStarletVBand(tmp, cur, next, band, energy, var, table, bi, 1 << j);
      const vv::BlurStep plan = vv::planGaussian(sp.window * float(1 << j), int(w), int(h));
      pyr.blurByPlan(energy, E, plan);
      B.dnPilot(band, E, var, pilot2, bi);
      pyr.blurByPlan(pilot2, E, plan);  // E now holds P
      B.dnAccumulate(out, band, E, var, bi);
      std::swap(cur, next);
    }
    B.dnFinish(src, dst, half, out, cur, f);
    return true;
  }

 private:
  Backend& B;
  vv::gpu::Pyramid<Backend> pyr;
  uint32_t cachedW = 0, cachedH = 0;
  Buf oppA{}, oppB{}, tmp{}, band{}, energy{}, E{}, var{}, pilot2{}, out{};

  void ensure(uint32_t w, uint32_t h) {
    if (w == cachedW && h == cachedH) return;
    cachedW = w;
    cachedH = h;
    B.releaseAll();
    const size_t n = size_t(w) * h;
    oppA = B.alloc(n); oppB = B.alloc(n); tmp = B.alloc(n);
    band = B.alloc(n); energy = B.alloc(n); E = B.alloc(n); var = B.alloc(n); pilot2 = B.alloc(n); out = B.alloc(n);
    pyr.allocate(w, h);
  }
};

}  // namespace gpu
}  // namespace dn
