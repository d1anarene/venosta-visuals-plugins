// Film Effects as a vv::Effect: everything the shared Premiere Pro and OpenFX adapters need.
#include <memory>

#include "FilmEffectsGpu.h"
#include "FilmEffectsParams.h"
#include "pipeline/pipeline.h"
#include "vv/effect.h"

namespace fe {
namespace {

class FilmEffect : public vv::Effect {
 public:
  const vv::EffectInfo& info() const override {
    static const vv::EffectInfo i = [] {
      vv::EffectInfo e{};
      e.name = "Film Effects";
      e.matchName = "VVIS FilmEffects";             // NEVER change: Premiere projects use it
      e.ofxId = "com.venostavisuals.FilmEffects";   // NEVER change: Resolve projects use it
      e.category = "Venosta Visuals";
      e.description = "Bloom, halation, film grain and film gate for finishing after the grade.";
      e.versionMajor = 1;
      e.versionMinor = 0;
      e.ofxParamPrefix = "fe";                       // NEVER change: Resolve saves "fe<diskId>"
      return e;
    }();
    return i;
  }

  const std::vector<vv::ParamSpec>& params() const override { return allParams(); }

  bool active(const vv::ParamReader& r) const override { return readParams(r).anyEffectActive(); }

  bool render(vv::Image& img, const vv::ParamReader& r, int64_t frameKey) const override {
    RenderContext ctx;
    ctx.frameKey = frameKey;
    return process(img, readParams(r), ctx);
  }

  // Applying a preset writes its values into the normal parameters (keyframeable, saved with the
  // project), then resets the menu to "(choose to apply)".
  bool paramChanged(int hostIndex, const vv::ParamReader& r, vv::ParamWriter& w) const override {
    const int menu = paramIndex(ParamKey::Preset);
    if (hostIndex != menu) return false;
    const char* name = presetMenuName(r.popup(menu));
    const Preset* preset = name ? findPreset(name) : nullptr;
    if (!preset || r.popup(menu) < 2) return false;
    forEachPresetValue(preset->params, [&](ParamKey k, double v) {
      const int i = paramIndex(k);
      if (i < 0) return;
      switch (specFor(k).kind) {
        case vv::ParamKind::Checkbox: w.setFlag(i, v != 0.0); break;
        case vv::ParamKind::Popup: w.setPopup(i, static_cast<int>(v)); break;
        case vv::ParamKind::Float:
        case vv::ParamKind::IntSlider: w.setNumber(i, v); break;
        default: break;
      }
    });
    w.setPopup(menu, 1);
    return true;
  }

  bool hasGpu(vv::GpuApi api) const override {
#if defined(FE_NO_GPU_RENDERERS)  // core tests: CPU only
    (void)api;
    return false;
#elif defined(__APPLE__)
    return api == vv::GpuApi::Metal;
#elif defined(FE_WITH_CUDA_RENDERER)
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
}  // namespace fe

namespace vv {
const Effect& effect() {
  static const fe::FilmEffect e;
  return e;
}
}  // namespace vv
