// Lens & Diffusion — GPU pass orchestration for any backend (CUDA, Metal, CPU emulation), making
// the CPU reference's decisions (src/core/lens.cpp). A Backend provides the vv::gpu::Pyramid
// operations plus:
//   void ldPrepare(const void* src, bool half, Buf lin, const FrameInfo&);
//   void ldAberrate(Buf lin, Buf out, const AberrationInfo&);
//   void ldWeights(Buf lin, Buf s, Buf b, const WeightInfo&);
//   void ldComposite(const void* src, void* dst, bool half, Buf lin, Buf s, Buf scat, Buf streak,
//                    const CompositeInfo&);
#pragma once
#include <cstdint>

#include "gpu/vv_gpu_pyramid.h"
#include "ld_gpu_pixels.h"
#include "lens.h"

namespace ld {
namespace gpu {

template <typename Backend>
class Pipeline {
 public:
  using Buf = typename Backend::Buf;
  explicit Pipeline(Backend& b) : B(b), pyr(b) {}

  /** One frame; src and dst may be the same buffer. F4 (32f) or H4 (16f) pixels with `rowPixels`
   *  stride; BGRA (Premiere) or RGBA (OpenFX). Row order does not matter (see ld_gpu_pixels.h). */
  bool run(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, const LensParams& raw,
           bool rgba) {
    if (w == 0 || h == 0) return false;
    const LensParams p = sanitized(raw);
    ensure(w, h);
    const PlaneInfo full{w, h};
    const bool doCa = p.aberration.enabled && p.aberration.amount != 0.f;
    const bool doDiff = p.diffusion.enabled && p.diffusion.amount > 0.f;
    const bool doStreak = p.streaks.enabled && p.streaks.intensity > 0.f;
    const bool doVig = p.vignette.enabled && p.vignette.amount != 0.f;

    B.ldPrepare(src, half, linA, FrameInfo{w, h, rowPixels, int32_t(p.space), rgba ? 1 : 0});
    Buf lin = linA;
    if (doCa) {
      B.ldAberrate(linA, linB, AberrationInfo{w, h, p.aberration.amount * 0.5f * float(h) / 1080.0f, p.aberration.edgeBias});
      lin = linB;
    }
    if (doDiff || doStreak)
      B.ldWeights(lin, s, b, WeightInfo{w, h, doDiff ? 1 : 0, doStreak ? 1 : 0, p.diffusion.highlightBias,
                                        p.streaks.threshold, p.streaks.knee});
    if (doDiff) {
      const DiffusionPsf psf = diffusionPsf(p.diffusion, int(h));
      B.clear(scat, full);
      for (int k = 0; k < 3; ++k) {
        pyr.blurByPlan(s, tmp, vv::planGaussian(psf.sigma[k], int(w), int(h)));
        B.axpy(scat, tmp, psf.weight[k], full);
      }
    }
    if (doStreak) {
      const StreakProfile prof = streakProfile(p.streaks, int(w));
      B.clear(streak, full);
      for (int k = 0; k < 3; ++k) {
        pyr.blurHorizontal(b, tmp, prof.sigma[k]);
        B.axpy(streak, tmp, prof.weight[k], full);
      }
    }
    CompositeInfo ci{};
    ci.width = w; ci.height = h; ci.rowPixels = rowPixels;
    ci.space = int32_t(p.space); ci.rgba = rgba ? 1 : 0;
    ci.doDiffusion = doDiff; ci.doStreaks = doStreak; ci.doVignette = doVig;
    ci.vignetteStyle = int32_t(p.vignette.style);
    ci.amount = p.diffusion.amount;
    const vv_float3 dt = ld_warmth_tint(p.diffusion.warmth);
    ci.diffTint[0] = dt.x; ci.diffTint[1] = dt.y; ci.diffTint[2] = dt.z;
    ci.intensity = p.streaks.intensity;
    const vv_float3 st = ld_streak_tint(int(p.streaks.color));
    ci.streakTint[0] = st.x; ci.streakTint[1] = st.y; ci.streakTint[2] = st.z;
    ci.vigAmount = p.vignette.amount; ci.vigSize = p.vignette.size;
    ci.vigRoundness = p.vignette.roundness; ci.vigFeather = p.vignette.feather;
    ci.mix = p.mix;
    B.ldComposite(src, dst, half, lin, s, scat, streak, ci);
    return true;
  }

 private:
  Backend& B;
  vv::gpu::Pyramid<Backend> pyr;
  uint32_t cachedW = 0, cachedH = 0;
  Buf linA{}, linB{}, s{}, b{}, scat{}, streak{}, tmp{};

  void ensure(uint32_t w, uint32_t h) {
    if (w == cachedW && h == cachedH) return;
    cachedW = w;
    cachedH = h;
    B.releaseAll();
    const size_t n = size_t(w) * h;
    linA = B.alloc(n); linB = B.alloc(n); s = B.alloc(n); b = B.alloc(n);
    scat = B.alloc(n); streak = B.alloc(n); tmp = B.alloc(n);
    pyr.allocate(w, h);
  }
};

}  // namespace gpu
}  // namespace ld
