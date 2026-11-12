// Film Effects — GPU pass orchestration, shared by the CUDA engine and the CPU emulation backend.
// Same pass sequence and buffer use as MetalFilmEngine::encode (src/gpu/metal).
//
// A Backend provides:
//   using Buf = ...;                         // device buffer handle (F4 elements)
//   Buf alloc(size_t elements);
//   void releaseAll();                       // free everything alloc() returned (size change)
//   void prepare(const void* frame, bool half, Buf lin, Buf bright, Buf halSrc, const FrameInfo&);
//   void downsample(Buf src, Buf dst, PlaneInfo s, PlaneInfo d);
//   void blurH(Buf src, Buf dst, PlaneInfo p, const std::vector<float>& weights);
//   void blurV(Buf src, Buf dst, PlaneInfo p, const std::vector<float>& weights);
//   void upsample(Buf src, Buf dst, PlaneInfo s, PlaneInfo d);
//   void axpy(Buf acc, Buf x, float w, PlaneInfo p);
//   void clear(Buf dst, PlaneInfo p);
//   void composite(const void* src, void* dst, bool half, Buf lin, Buf bloom, Buf halNear, Buf halFar,
//                  Buf halSrc, const CompositeInfo&);
//   void compositeFilm(const void* src, bool half, Buf film, Buf lin, Buf bloom, Buf halNear, Buf halFar,
//                      Buf halSrc, const CompositeInfo&);   // Film Gate only
//   void gate(const void* src, void* dst, bool half, Buf film, const GateInfo&);  // Film Gate only
// Every call must complete before the next one starts reading its output (in-order stream).
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "blur/blur.h"
#include "blur/blur_plan.h"
#include "common/fe_kernels.h"
#include "fe_gpu_pixels.h"
#include "gate/gate.h"
#include "params/params.h"

namespace fe {
namespace gpu {

inline PlaneInfo halfPlane(PlaneInfo p) { return {std::max(1u, (p.width + 1) / 2), std::max(1u, (p.height + 1) / 2)}; }

/** Values for the composite pass, computed exactly like the CPU pipeline and the Metal engine. */
inline CompositeInfo makeCompositeInfo(const FilmParams& p, uint32_t w, uint32_t h, uint32_t rowPixels,
                                       const HalationPlan& hplan, int64_t frameKey) {
  CompositeInfo ci{};
  ci.width = w;
  ci.height = h;
  ci.rowPixels = rowPixels;
  ci.space = int32_t(p.space);
  ci.doBloom = p.bloom.enabled && p.bloom.intensity > 0.f;
  ci.doHal = p.halation.enabled && p.halation.intensity > 0.f;
  ci.doGrain = p.grain.enabled && p.grain.amount > 0.f;
  ci.bloomIntensity = p.bloom.intensity;
  ci.halNearWeight = hplan.nearWeight;
  ci.halFarWeight = hplan.farWeight;
  ci.halGain = p.halation.intensity * 4.0f;  // same kGain as src/core/halation
  vv_float3 tint = fe_halation_tint(p.halation.warmth);
  ci.halTint[0] = tint.x;
  ci.halTint[1] = tint.y;
  ci.halTint[2] = tint.z;
  const float sizePx = p.grain.size * float(h) / 1080.0f;
  ci.grainAmp = p.grain.amount * 0.12f * (sizePx < 1.0f ? sizePx : 1.0f);
  ci.grainSizePx = sizePx;
  ci.grainRoughness = p.grain.roughness;
  ci.grainShadows = p.grain.shadows;
  ci.grainMids = p.grain.midtones;
  ci.grainHighs = p.grain.highlights;
  ci.grainColor = p.grain.color == GrainColor::Color ? 1 : 0;
  ci.grainChroma = p.grain.chroma;
  const uint64_t t = p.grain.animate ? uint64_t(frameKey) : 0u;
  ci.grainKey = vv_hash3(vv_uint(p.grain.seed), vv_uint(t & 0xffffffffu), vv_uint(t >> 32));
  ci.mix = p.mix;
  return ci;
}

/** GPU form of the per-frame gate values (computed on the host by the tested core). */
inline GateInfo makeGateInfo(const GateFrame& f, uint32_t w, uint32_t h, uint32_t rowPixels, int32_t space, bool rgba,
                             bool bottomUp, float mix) {
  GateInfo g{};
  g.width = w; g.height = h; g.rowPixels = rowPixels;
  g.space = space; g.rgba = rgba ? 1 : 0; g.bottomUp = bottomUp ? 1 : 0;
  g.doWeave = f.doWeave; g.doDefects = f.doDefects; g.doEdge = f.doEdge;
  g.look = f.look; g.scratchCount = f.scratchCount;
  g.dx = f.dx; g.dy = f.dy; g.gain = f.gain;
  g.insetPx = f.insetPx; g.softPx = f.softPx; g.radiusPx = f.radiusPx;
  g.dustCellPx = f.dustCellPx; g.dustDensity = f.dustDensity; g.dustSizePx = f.dustSizePx; g.dustKey = f.dustKey;
  g.mix = mix;
  for (int k = 0; k < kMaxScratches; ++k) {
    g.sx0[k] = f.scratches[k].x0; g.sslope[k] = f.scratches[k].slope; g.swidth[k] = f.scratches[k].widthPx;
    g.sstrength[k] = f.scratches[k].strength; g.skey[k] = f.scratches[k].key; g.ssign[k] = f.scratches[k].sign;
  }
  return g;
}

template <typename Backend>
class Pipeline {
 public:
  using Buf = typename Backend::Buf;
  explicit Pipeline(Backend& b) : B(b) {}

  /** Run the effect on one frame. `src` and `dst` may be the same buffer. Frames are F4 (32f) or
   *  H4 (16f) per pixel, `rowPixels` stride; BGRA top-down (Premiere) by default, or RGBA and/or
   *  bottom-up rows (OpenFX). Returns false for an empty frame. */
  bool run(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, const FilmParams& raw,
           int64_t frameKey, bool rgba = false, bool bottomUp = false) {
    if (w == 0 || h == 0) return false;
    const FilmParams p = sanitized(raw);
    ensure(w, h);
    const bool doBloom = p.bloom.enabled && p.bloom.intensity > 0.f;
    const bool doHal = p.halation.enabled && p.halation.intensity > 0.f;

    if (doBloom || doHal) {
      FrameInfo fi{w, h, rowPixels, int32_t(p.space), p.bloom.threshold, p.bloom.knee, p.halation.threshold,
                   p.halation.knee, rgba ? 1 : 0};
      B.prepare(src, half, lin, levels[0], halSrc, fi);
    }

    if (doBloom) {
      const BloomPlan plan = planBloom(p.bloom, p.quality, int(w), int(h));
      for (int L = 1; L <= plan.maxLevel; ++L) B.downsample(levels[L - 1], levels[L], dims[L - 1], dims[L]);
      for (int L = plan.maxLevel; L >= 0; --L) {
        if (L == plan.maxLevel) B.clear(accB[L], dims[L]);
        else B.upsample(accB[L + 1], accB[L], dims[L + 1], dims[L]);
        for (size_t s = 0; s < plan.scales.size(); ++s) {
          if (plan.scales[s].level != L) continue;
          blur(levels[L], tmpB[L], tmpA[L], dims[L], plan.scales[s].sigmaAtLevel);
          B.axpy(accB[L], tmpA[L], plan.weights[s], dims[L]);
        }
      }
    }

    HalationPlan hplan;
    if (doHal) {
      hplan = planHalation(p.halation, int(w), int(h));
      blurByPlan(halSrc, halNear, hplan.nearStep);
      blurByPlan(halSrc, halFar, hplan.farStep);
    }

    CompositeInfo ci = makeCompositeInfo(p, w, h, rowPixels, hplan, frameKey);
    ci.rgba = rgba ? 1 : 0;
    ci.bottomUp = bottomUp ? 1 : 0;
    const GateFrame gf = gateFrame(p.gate, int(w), int(h), frameKey);
    if (gf.active) {
      // Film image first (no mix), then the gate pass reads it with the weave offset and mixes.
      if (!filmAllocated) {
        film = B.alloc(size_t(w) * h);
        filmAllocated = true;
      }
      B.compositeFilm(src, half, film, lin, accB[0], halNear, halFar, halSrc, ci);
      B.gate(src, dst, half, film, makeGateInfo(gf, w, h, rowPixels, int32_t(p.space), rgba, bottomUp, p.mix));
    } else {
      B.composite(src, dst, half, lin, accB[0], halNear, halFar, halSrc, ci);
    }
    return true;
  }

 private:
  Backend& B;
  uint32_t cachedW = 0, cachedH = 0;
  Buf lin{}, halSrc{}, halNear{}, halFar{}, film{};
  bool filmAllocated = false;  // allocated on first use (Film Gate), released with the others
  std::vector<Buf> levels, accA, accB, tmpA, tmpB;
  std::vector<PlaneInfo> dims;

  void ensure(uint32_t w, uint32_t h) {
    if (w == cachedW && h == cachedH) return;
    cachedW = w;
    cachedH = h;
    B.releaseAll();  // previous frame size's buffers (no work is in flight between frames)
    filmAllocated = false;
    const size_t full = size_t(w) * h;
    lin = B.alloc(full);
    halSrc = B.alloc(full);
    halNear = B.alloc(full);
    halFar = B.alloc(full);
    dims.clear();
    levels.clear(); accA.clear(); accB.clear(); tmpA.clear(); tmpB.clear();
    PlaneInfo p{w, h};
    for (int L = 0; L < 14; ++L) {
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

  // Separable Gaussian src -> dst via tmp; sigma < 0.05 copies (same rule as the CPU core).
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

  // Equivalent of the CPU blurByPlan: downsample `level` times, blur, upsample back into `out`.
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
};

}  // namespace gpu
}  // namespace fe
