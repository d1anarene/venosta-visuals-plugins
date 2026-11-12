// Exposure & False Color as a vv::Effect for the shared Premiere Pro and OpenFX adapters.
#include "EfGpu.h"
#include "EfParamsTable.h"
#include "vv/effect.h"

namespace ef {
namespace {

class EfEffect : public vv::Effect {
 public:
  const vv::EffectInfo& info() const override {
    static const vv::EffectInfo i = [] {
      vv::EffectInfo e{};
      e.name = "Exposure & False Color";
      e.matchName = "VVIS ExposureFalseColor";             // NEVER change: Premiere projects use it
      e.ofxId = "com.venostavisuals.ExposureFalseColor";   // NEVER change: Resolve projects use it
      e.category = "Venosta Visuals";
      e.description = "Viewing aid: false color, zebras, clipping, split view. Turn off before export.";
      e.versionMajor = 1;
      e.versionMinor = 0;
      e.ofxParamPrefix = "ef";                              // NEVER change
      return e;
    }();
    return i;
  }
  const std::vector<vv::ParamSpec>& params() const override { return allParams(); }
  bool active(const vv::ParamReader& r) const override { return readParams(r).anyActive(); }
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
#if defined(EF_NO_GPU_RENDERERS)
    (void)api;
    return false;
#elif defined(__APPLE__)
    return api == vv::GpuApi::Metal;
#elif defined(EF_WITH_CUDA_RENDERER)
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
}  // namespace ef

namespace vv {
const Effect& effect() {
  static const ef::EfEffect e;
  return e;
}
}  // namespace vv
