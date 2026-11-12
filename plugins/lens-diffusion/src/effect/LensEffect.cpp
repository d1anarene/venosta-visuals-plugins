// Lens & Diffusion as a vv::Effect for the shared Premiere Pro and OpenFX adapters.
// GPU: Metal (macOS) and CUDA (builds with the CUDA toolkit) renderers, CPU otherwise.
#include "LensGpu.h"
#include "LensParamsTable.h"
#include "lens.h"
#include "vv/effect.h"

namespace ld {
namespace {

class LensEffect : public vv::Effect {
 public:
  const vv::EffectInfo& info() const override {
    static const vv::EffectInfo i = [] {
      vv::EffectInfo e{};
      e.name = "Lens & Diffusion";
      e.matchName = "VVIS LensDiffusion";             // NEVER change: Premiere projects use it
      e.ofxId = "com.venostavisuals.LensDiffusion";   // NEVER change: Resolve projects use it
      e.category = "Venosta Visuals";
      e.description = "Diffusion filter, vignette, chromatic aberration and anamorphic streaks.";
      e.versionMajor = 1;
      e.versionMinor = 0;
      e.ofxParamPrefix = "ld";                         // NEVER change: Resolve saves "ld<diskId>"
      return e;
    }();
    return i;
  }

  const std::vector<vv::ParamSpec>& params() const override { return allParams(); }
  bool active(const vv::ParamReader& r) const override { return readParams(r).anyEffectActive(); }
  bool render(vv::Image& img, const vv::ParamReader& r, int64_t) const override { return process(img, readParams(r)); }

  bool paramChanged(int hostIndex, const vv::ParamReader& r, vv::ParamWriter& w) const override {
    const int menu = paramIndex(ParamKey::Preset);
    if (hostIndex != menu || r.popup(menu) < 2) return false;
    const char* name = presetMenuName(r.popup(menu));
    const Preset* preset = name ? findPreset(name) : nullptr;
    if (!preset) return false;
    forEachPresetValue(preset->params, [&](ParamKey k, double v) {
      const int i = paramIndex(k);
      switch (specFor(k).kind) {
        case vv::ParamKind::Checkbox: w.setFlag(i, v != 0.0); break;
        case vv::ParamKind::Popup: w.setPopup(i, static_cast<int>(v)); break;
        default: w.setNumber(i, v); break;
      }
    });
    w.setPopup(menu, 1);
    return true;
  }

  bool hasGpu(vv::GpuApi api) const override {
#if defined(LD_NO_GPU_RENDERERS)  // core tests: CPU only
    (void)api;
    return false;
#elif defined(__APPLE__)
    return api == vv::GpuApi::Metal;
#elif defined(LD_WITH_CUDA_RENDERER)
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
}  // namespace ld

namespace vv {
const Effect& effect() {
  static const ld::LensEffect e;
  return e;
}
}  // namespace vv
