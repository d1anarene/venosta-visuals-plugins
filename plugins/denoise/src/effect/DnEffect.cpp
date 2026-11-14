// Denoise as a vv::Effect (RESEARCH PROTOTYPE, not released).
// Noise profile: estimated every frame on a centred crop (two-frame temporal estimate when a
// neighbour exists, otherwise the single-frame spatial estimate), scaled by Noise Level. Temporal
// fusion where the host supplies neighbours (OpenFX temporal clip access, CPU); spatial only
// otherwise. GPU (Premiere only, see CMakeLists.txt): spatial filter on CUDA / Metal.
#include <cmath>

#include "DnGpu.h"
#include "DnParamsTable.h"
#include "dn_profile.h"
#include "dn_spatial.h"
#include "dn_temporal.h"
#include "vv/effect.h"

namespace dn {
namespace {

struct Profiles {
  NoiseProfile starlet;
  PixelSigma pixel;
};

Profiles estimate(const vv::Image& cur, const vv::Image* other, float level) {
  const vv::Plane a = toRgbPlane(centreCrop(cur));
  Profiles p;
  if (other && other->width == cur.width && other->height == cur.height) {
    const vv::Plane b = toRgbPlane(centreCrop(*other));
    p.starlet = estimateProfileTemporal(a, b, nullptr, Domain::Starlet);
    p.pixel = pixelSigmaFrom(estimateProfileTemporal(a, b, nullptr, Domain::Pixel));
  } else {
    p.starlet = estimateProfile(a, nullptr, Domain::Starlet);
    p.pixel = pixelSigmaFrom(estimateProfile(a, nullptr, Domain::Pixel));
  }
  p.starlet = p.starlet.scaled(level);
  for (auto& row : p.pixel.sigma)
    for (float& v : row) v *= level;
  return p;
}

void finish(vv::Image& img, const vv::Image& input, const vv::Image& result, const EffectParams& ep) {
  for (size_t i = 0; i < img.px.size(); ++i) {
    if (i % 4 == 3) continue;  // alpha unchanged
    const float in = input.px[i], r = result.px[i];
    float o = ep.view == 1 ? 0.5f + 4.f * (in - r) : (ep.view == 2 ? in : r);
    if (ep.view == 0 && ep.mix < 1.f) o = in + (o - in) * ep.mix;
    img.px[i] = std::isfinite(o) ? o : in;
  }
}

class DnEffect : public vv::Effect {
 public:
  const vv::EffectInfo& info() const override {
    static const vv::EffectInfo i = [] {
      vv::EffectInfo e{};
      e.name = "Denoise (research prototype)";
      e.matchName = "VVIS Denoise";
      e.ofxId = "com.venostavisuals.Denoise";
      e.category = "Venosta Visuals";
      e.description = "Research prototype: temporal + spatial noise reduction.";
      e.versionMajor = 1;
      e.versionMinor = 0;
      e.ofxParamPrefix = "dn";
      return e;
    }();
    return i;
  }
  const std::vector<vv::ParamSpec>& params() const override { return allParams(); }
  bool active(const vv::ParamReader& r) const override {
    const EffectParams p = readParams(r);
    return p.masterEnable && (p.mix > 0.f || p.view != 0);
  }
  int maxTemporalRadius() const override { return 3; }
  int temporalRadius(const vv::ParamReader& r) const override { return readParams(r).radius; }

  bool render(vv::Image& img, const vv::ParamReader& r, int64_t) const override {
    const EffectParams ep = readParams(r);
    const NoiseProfile prof = estimateSpatialProfile(centreCrop(img, cropW(ep), cropH(ep)), ep.noiseLevel);
    const vv::Image input = img;
    finish(img, input, denoiseSpatial(input, prof, spatialParams(ep)), ep);
    return true;
  }

  bool renderTemporal(vv::Image& img, const std::vector<const vv::Image*>& window, int radius, const vv::ParamReader& r,
                      int64_t key) const override {
    const EffectParams ep = readParams(r);
    std::vector<const vv::Image*> frames;
    int centre = 0;
    for (int k = -radius; k <= radius; ++k) {
      const vv::Image* f = window[size_t(k + radius)];
      if (k == 0) { centre = int(frames.size()); frames.push_back(f); continue; }
      if (f && f->width == img.width && f->height == img.height) frames.push_back(f);
    }
    if (frames.size() < 2) return render(img, r, key);
    const vv::Image* other = frames[size_t(centre == 0 ? 1 : centre - 1)];  // nearest earlier (or later) frame
    const Profiles pr = estimate(img, other, ep.noiseLevel);
    const SpatialParams sp = spatialParams(ep);
    TemporalParams tp;
    tp.postStrength = 1.f - ep.detail;
    const vv::Image input = img;
    finish(img, input, denoiseTemporal(frames, centre, pr.starlet, pr.pixel, sp, tp), ep);
    return true;
  }

  bool paramChanged(int hostIndex, const vv::ParamReader& r, vv::ParamWriter& w) const override {
    const int menu = paramIndex(ParamKey::Preset);
    if (hostIndex != menu || r.popup(menu) < 2) return false;
    int count = 0;
    const Preset* list = presets(count);
    const int i = r.popup(menu) - 2;
    if (i < 0 || i >= count) return false;
    const Preset& p = list[i];
    w.setNumber(paramIndex(ParamKey::LumaStrength), p.luma);
    w.setNumber(paramIndex(ParamKey::ChromaStrength), p.chroma);
    w.setNumber(paramIndex(ParamKey::Detail), p.detail * 100.0);
    w.setNumber(paramIndex(ParamKey::NoiseLevel), p.noiseLevel);
    w.setPopup(menu, 1);
    return true;
  }

  bool hasGpu(vv::GpuApi api) const override {
#if defined(DN_WITH_METAL_RENDERER)
    return api == vv::GpuApi::Metal;
#elif defined(DN_WITH_CUDA_RENDERER)
    return api == vv::GpuApi::Cuda;
#else
    (void)api;
    return false;
#endif
  }
  std::unique_ptr<vv::GpuRenderer> makeGpuRenderer(vv::GpuApi api) const override {
    if (!hasGpu(api)) return nullptr;
    return api == vv::GpuApi::Metal ? makeMetalRenderer() : makeCudaRenderer();
  }
};

}  // namespace
}  // namespace dn

namespace vv {
const Effect& effect() {
  static const dn::DnEffect e;
  return e;
}
}  // namespace vv
