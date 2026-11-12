// Film Effects — Metal backend implementation. Mirrors src/core/pipeline exactly, pass for pass,
// using the shared blur plans (blur_plan.h) and Gaussian weights (gaussianKernel).
// STATUS: NOT YET COMPILED (requires macOS + Xcode). See docs/FEASIBILITY.md.
#import "MetalFilmEngine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <simd/simd.h>
#include <vector>

#include "blur/blur.h"
#include "blur/blur_plan.h"
#include "common/fe_kernels.h"
#include "gate/gate.h"

namespace fe {
namespace {

// Must match the structs in FilmEffects.metal (same member order and types).
struct FrameInfo {
  uint32_t width, height, rowPixels;
  int32_t space;
  float bloomThreshold, bloomKnee, halThreshold, halKnee;
  int32_t rgba;
};
struct PlaneInfo {
  uint32_t width, height;
};
struct BlurInfo {
  uint32_t width, height;
  int32_t radius;
};
struct CompositeInfo {
  uint32_t width, height, rowPixels;
  int32_t space, doBloom, doHal, doGrain;
  float bloomIntensity, halNearWeight, halFarWeight, halGain;
  simd_float3 halTint;
  float grainAmp, grainSizePx, grainRoughness, grainShadows, grainMids, grainHighs;
  int32_t grainColor;
  float grainChroma;
  uint32_t grainKey;
  float mix;
  int32_t rgba;
  int32_t bottomUp;
};

// Must match GateInfo in FilmEffects.metal (4-byte members only, same order).
struct GateInfo {
  uint32_t width, height, rowPixels;
  int32_t space, rgba, bottomUp;
  int32_t doWeave, doDefects, doEdge, look, scratchCount;
  float dx, dy, gain;
  float insetPx, softPx, radiusPx;
  float dustCellPx, dustDensity, dustSizePx;
  uint32_t dustKey;
  float mix;
  float sx0[4], sslope[4], swidth[4], sstrength[4];
  uint32_t skey[4];
  int32_t ssign[4];
};

PlaneInfo half(PlaneInfo p) { return {std::max(1u, (p.width + 1) / 2), std::max(1u, (p.height + 1) / 2)}; }

}  // namespace

struct MetalFilmEngine::Impl {
  id<MTLDevice> device = nil;
  std::map<std::string, id<MTLComputePipelineState>> pipelines;
  std::string error;

  // Scratch buffers are cached per frame size and reused between frames.
  uint32_t cachedW = 0, cachedH = 0;
  id<MTLBuffer> lin = nil, halSrc = nil, halNear = nil, halFar = nil;
  id<MTLBuffer> film = nil;  // Film Gate only, allocated on first use
  std::vector<id<MTLBuffer>> levels;  // bloom pyramid levels (level 0 = bright, full res)
  std::vector<id<MTLBuffer>> accA, accB, tmpA, tmpB;  // per-level scratch
  std::vector<PlaneInfo> dims;

  id<MTLComputePipelineState> pso(const char* name) {
    auto it = pipelines.find(name);
    return it == pipelines.end() ? nil : it->second;
  }

  id<MTLBuffer> buf(size_t bytes) { return [device newBufferWithLength:std::max<size_t>(bytes, 16) options:MTLResourceStorageModePrivate]; }

  void ensure(uint32_t w, uint32_t h) {
    if (w == cachedW && h == cachedH) return;
    cachedW = w;
    cachedH = h;
    const size_t full = size_t(w) * h * sizeof(float) * 4;
    lin = buf(full);
    halSrc = buf(full);
    halNear = buf(full);
    halFar = buf(full);
    film = nil;
    dims.clear();
    levels.clear();
    accA.clear(); accB.clear(); tmpA.clear(); tmpB.clear();
    PlaneInfo p{w, h};
    for (int L = 0; L < 14; ++L) {  // enough levels for any frame size
      dims.push_back(p);
      size_t bytes = size_t(p.width) * p.height * sizeof(float) * 4;
      levels.push_back(buf(bytes));
      accA.push_back(buf(bytes));
      accB.push_back(buf(bytes));
      tmpA.push_back(buf(bytes));
      tmpB.push_back(buf(bytes));
      if (p.width == 1 && p.height == 1) break;
      p = half(p);
    }
  }

  static void dispatch(id<MTLComputeCommandEncoder> enc, id<MTLComputePipelineState> ps, uint32_t w, uint32_t h) {
    [enc setComputePipelineState:ps];
    NSUInteger tw = ps.threadExecutionWidth;
    NSUInteger th = std::max<NSUInteger>(1, ps.maxTotalThreadsPerThreadgroup / tw);
    [enc dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
  }

  // Separable Gaussian src -> dst (tmp used for the horizontal pass). sigma < 0.05: plain copy.
  void blur(id<MTLComputeCommandEncoder> enc, id<MTLBuffer> src, id<MTLBuffer> tmp, id<MTLBuffer> dst, PlaneInfo p,
            float sigma) {
    if (sigma < 0.05f) {
      // Same as CPU: no blur. Copy via axpy into a cleared buffer.
      [enc setBuffer:dst offset:0 atIndex:0];
      [enc setBytes:&p length:sizeof(p) atIndex:1];
      dispatch(enc, pso("fe_clear"), p.width, p.height);
      float one = 1.0f;
      [enc setBuffer:dst offset:0 atIndex:0];
      [enc setBuffer:src offset:0 atIndex:1];
      [enc setBytes:&one length:sizeof(one) atIndex:2];
      [enc setBytes:&p length:sizeof(p) atIndex:3];
      dispatch(enc, pso("fe_axpy"), p.width, p.height);
      return;
    }
    std::vector<float> k = gaussianKernel(sigma);
    BlurInfo bi{p.width, p.height, int32_t(k.size() / 2)};
    // Kernels stay small because the plan keeps per-level sigma low; setBytes avoids a per-frame
    // allocation (limit 4 KB = 1024 taps).
    if (k.size() * sizeof(float) > 4096) k.resize(1);  // cannot happen with planGaussian; fail visibly
    [enc setBuffer:src offset:0 atIndex:0];
    [enc setBuffer:tmp offset:0 atIndex:1];
    [enc setBytes:&bi length:sizeof(bi) atIndex:2];
    [enc setBytes:k.data() length:k.size() * sizeof(float) atIndex:3];
    dispatch(enc, pso("fe_blur_h"), p.width, p.height);
    [enc setBuffer:tmp offset:0 atIndex:0];
    [enc setBuffer:dst offset:0 atIndex:1];
    dispatch(enc, pso("fe_blur_v"), p.width, p.height);
  }

  void downsample(id<MTLComputeCommandEncoder> enc, id<MTLBuffer> src, id<MTLBuffer> dst, PlaneInfo s, PlaneInfo d) {
    [enc setBuffer:src offset:0 atIndex:0];
    [enc setBuffer:dst offset:0 atIndex:1];
    [enc setBytes:&s length:sizeof(s) atIndex:2];
    [enc setBytes:&d length:sizeof(d) atIndex:3];
    dispatch(enc, pso("fe_downsample"), d.width, d.height);
  }

  void upsample(id<MTLComputeCommandEncoder> enc, id<MTLBuffer> src, id<MTLBuffer> dst, PlaneInfo s, PlaneInfo d) {
    [enc setBuffer:src offset:0 atIndex:0];
    [enc setBuffer:dst offset:0 atIndex:1];
    [enc setBytes:&s length:sizeof(s) atIndex:2];
    [enc setBytes:&d length:sizeof(d) atIndex:3];
    dispatch(enc, pso("fe_upsample"), d.width, d.height);
  }

  // Equivalent of CPU blurByPlan: downsample `level` times, blur, upsample back. Result in `out`.
  void blurByPlan(id<MTLComputeCommandEncoder> enc, id<MTLBuffer> src, id<MTLBuffer> out, const BlurStep& step) {
    id<MTLBuffer> cur = src;
    for (int L = 1; L <= step.level; ++L) {
      downsample(enc, cur, tmpA[L], dims[L - 1], dims[L]);
      cur = tmpA[L];
    }
    id<MTLBuffer> blurred = step.level == 0 ? out : accA[step.level];
    blur(enc, cur, tmpB[step.level], blurred, dims[step.level], step.sigmaAtLevel);
    for (int L = step.level - 1; L >= 0; --L) {
      id<MTLBuffer> dst = L == 0 ? out : accA[L];
      upsample(enc, blurred, dst, dims[L + 1], dims[L]);
      blurred = dst;
    }
  }
};

MetalFilmEngine::MetalFilmEngine(id<MTLDevice> device, const std::string& metallibPath) : impl_(new Impl) {
  impl_->device = device;
  NSError* err = nil;
  NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:metallibPath.c_str()]];
  id<MTLLibrary> lib = [device newLibraryWithURL:url error:&err];
  if (!lib) {
    impl_->error = "Could not load FilmEffects.metallib";
    return;
  }
  for (const char* name : {"fe_prepare_f32", "fe_prepare_f16", "fe_downsample", "fe_blur_h", "fe_blur_v",
                           "fe_upsample", "fe_axpy", "fe_clear", "fe_composite_f32", "fe_composite_f16",
                           "fe_composite_film_f32", "fe_composite_film_f16", "fe_gate_f32", "fe_gate_f16"}) {
    id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:name]];
    id<MTLComputePipelineState> ps = fn ? [device newComputePipelineStateWithFunction:fn error:&err] : nil;
    if (!ps) {
      impl_->error = std::string("Missing Metal kernel ") + name;
      return;
    }
    impl_->pipelines[name] = ps;
  }
}

MetalFilmEngine::~MetalFilmEngine() = default;
bool MetalFilmEngine::ok() const { return impl_ && impl_->error.empty(); }
const std::string& MetalFilmEngine::error() const { return impl_->error; }

bool MetalFilmEngine::encode(id<MTLCommandBuffer> cb, const MetalFrame& src, const MetalFrame& dst,
                             const FilmParams& raw, int64_t frameKey) {
  if (!ok() || src.width == 0 || src.height == 0) return false;
  const FilmParams p = sanitized(raw);
  Impl& I = *impl_;
  const uint32_t w = src.width, h = src.height;
  I.ensure(w, h);

  const bool doBloom = p.bloom.enabled && p.bloom.intensity > 0.f;
  const bool doHal = p.halation.enabled && p.halation.intensity > 0.f;
  const bool doGrain = p.grain.enabled && p.grain.amount > 0.f;

  id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
  // The default compute encoder uses MTLDispatchTypeSerial: each dispatch finishes before the next
  // starts, so every pass sees the complete output of the previous one (deterministic results).

  if (doBloom || doHal) {
    FrameInfo fi{w, h, src.rowPixels, int32_t(p.space), p.bloom.threshold, p.bloom.knee, p.halation.threshold,
                 p.halation.knee, src.rgba ? 1 : 0};
    [enc setBuffer:src.buffer offset:0 atIndex:0];
    [enc setBuffer:I.lin offset:0 atIndex:1];
    [enc setBuffer:I.levels[0] offset:0 atIndex:2];
    [enc setBuffer:I.halSrc offset:0 atIndex:3];
    [enc setBytes:&fi length:sizeof(fi) atIndex:4];
    Impl::dispatch(enc, I.pso(src.halfFloat ? "fe_prepare_f16" : "fe_prepare_f32"), w, h);
  }

  id<MTLBuffer> bloomResult = I.accB[0];
  if (doBloom) {
    const BloomPlan plan = planBloom(p.bloom, p.quality, int(w), int(h));
    for (int L = 1; L <= plan.maxLevel; ++L) I.downsample(enc, I.levels[L - 1], I.levels[L], I.dims[L - 1], I.dims[L]);
    // acc ping-pongs between accB (result per level) and tmpA (blurred scale)
    for (int L = plan.maxLevel; L >= 0; --L) {
      PlaneInfo d = I.dims[L];
      if (L == plan.maxLevel) {
        [enc setBuffer:I.accB[L] offset:0 atIndex:0];
        [enc setBytes:&d length:sizeof(d) atIndex:1];
        Impl::dispatch(enc, I.pso("fe_clear"), d.width, d.height);
      } else {
        I.upsample(enc, I.accB[L + 1], I.accB[L], I.dims[L + 1], d);
      }
      for (size_t s = 0; s < plan.scales.size(); ++s) {
        if (plan.scales[s].level != L) continue;
        I.blur(enc, I.levels[L], I.tmpB[L], I.tmpA[L], d, plan.scales[s].sigmaAtLevel);
        float wgt = plan.weights[s];
        [enc setBuffer:I.accB[L] offset:0 atIndex:0];
        [enc setBuffer:I.tmpA[L] offset:0 atIndex:1];
        [enc setBytes:&wgt length:sizeof(wgt) atIndex:2];
        [enc setBytes:&d length:sizeof(d) atIndex:3];
        Impl::dispatch(enc, I.pso("fe_axpy"), d.width, d.height);
      }
    }
    bloomResult = I.accB[0];
  }

  HalationPlan hplan;
  if (doHal) {
    hplan = planHalation(p.halation, int(w), int(h));
    I.blurByPlan(enc, I.halSrc, I.halNear, hplan.nearStep);
    I.blurByPlan(enc, I.halSrc, I.halFar, hplan.farStep);
  }

  CompositeInfo ci{};
  ci.width = w;
  ci.height = h;
  ci.rowPixels = src.rowPixels;
  ci.space = int32_t(p.space);
  ci.doBloom = doBloom;
  ci.doHal = doHal;
  ci.doGrain = doGrain;
  ci.bloomIntensity = p.bloom.intensity;
  ci.halNearWeight = hplan.nearWeight;
  ci.halFarWeight = hplan.farWeight;
  ci.halGain = p.halation.intensity * 4.0f;  // same kGain as src/core/halation
  fe_float3 tint = fe_halation_tint(p.halation.warmth);
  ci.halTint = simd_make_float3(tint.x, tint.y, tint.z);
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
  ci.grainKey = fe_hash3(fe_uint(p.grain.seed), fe_uint(t & 0xffffffffu), fe_uint(t >> 32));
  ci.mix = p.mix;
  ci.rgba = src.rgba ? 1 : 0;
  ci.bottomUp = src.bottomUp ? 1 : 0;

  const GateFrame gf = gateFrame(p.gate, int(w), int(h), frameKey);
  if (gf.active && !I.film) I.film = I.buf(size_t(w) * h * sizeof(float) * 4);
  [enc setBuffer:src.buffer offset:0 atIndex:0];
  [enc setBuffer:(gf.active ? I.film : dst.buffer) offset:0 atIndex:1];
  [enc setBuffer:I.lin offset:0 atIndex:2];
  [enc setBuffer:bloomResult offset:0 atIndex:3];
  [enc setBuffer:I.halNear offset:0 atIndex:4];
  [enc setBuffer:I.halFar offset:0 atIndex:5];
  [enc setBuffer:I.halSrc offset:0 atIndex:6];
  [enc setBytes:&ci length:sizeof(ci) atIndex:7];
  if (!gf.active) {
    Impl::dispatch(enc, I.pso(src.halfFloat ? "fe_composite_f16" : "fe_composite_f32"), w, h);
  } else {
    // Film Gate: film image (no mix) first, then weave / defects / flicker / aperture and mix.
    Impl::dispatch(enc, I.pso(src.halfFloat ? "fe_composite_film_f16" : "fe_composite_film_f32"), w, h);
    GateInfo g{};
    g.width = w; g.height = h; g.rowPixels = src.rowPixels;
    g.space = int32_t(p.space); g.rgba = src.rgba ? 1 : 0; g.bottomUp = src.bottomUp ? 1 : 0;
    g.doWeave = gf.doWeave; g.doDefects = gf.doDefects; g.doEdge = gf.doEdge;
    g.look = gf.look; g.scratchCount = gf.scratchCount;
    g.dx = gf.dx; g.dy = gf.dy; g.gain = gf.gain;
    g.insetPx = gf.insetPx; g.softPx = gf.softPx; g.radiusPx = gf.radiusPx;
    g.dustCellPx = gf.dustCellPx; g.dustDensity = gf.dustDensity; g.dustSizePx = gf.dustSizePx;
    g.dustKey = gf.dustKey;
    g.mix = p.mix;
    for (int k = 0; k < kMaxScratches; ++k) {
      g.sx0[k] = gf.scratches[k].x0; g.sslope[k] = gf.scratches[k].slope; g.swidth[k] = gf.scratches[k].widthPx;
      g.sstrength[k] = gf.scratches[k].strength; g.skey[k] = gf.scratches[k].key; g.ssign[k] = gf.scratches[k].sign;
    }
    [enc setBuffer:src.buffer offset:0 atIndex:0];
    [enc setBuffer:dst.buffer offset:0 atIndex:1];
    [enc setBuffer:I.film offset:0 atIndex:2];
    [enc setBytes:&g length:sizeof(g) atIndex:3];
    Impl::dispatch(enc, I.pso(src.halfFloat ? "fe_gate_f16" : "fe_gate_f32"), w, h);
  }
  [enc endEncoding];
  return true;
}

}  // namespace fe
