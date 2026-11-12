// Film Effects — OpenFX plug-in (DaVinci Resolve and other OFX hosts).
//
// One effect, Film Effects, with every tool in one panel (standalone Bloom/Halation/Grain effects
// existed until v1.0.11) — the
// same variants, parameters and core renderer as the Premiere plug-ins (src/core). Built on the
// BSD-licensed OpenFX C++ Support library (fetched at configure time, see ofx.cmake).
//
// Rendering: CPU through the tested core; GPU through the OFX GPU render extensions with the same
// engines as Premiere — Metal on macOS, CUDA where the build has the CUDA toolkit (Windows). The
// host chooses: if it enables GPU rendering for a frame, the images are GPU buffers.
// STATUS: compiled and loaded by tests/ofx/ofx_load_test.cpp in CI; the GPU pass maths is checked
// in OpenFX layout by tests/gpu. NOT yet run inside DaVinci Resolve.

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"

#include "FilmEffectsOFXGpu.h"
#include "FilmEffectsParams.h"  // SDK-free parameter table shared with the Premiere plug-ins
#include "pipeline/pipeline.h"

namespace {

constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 0;
constexpr const char* kGrouping = "Venosta Visuals";

struct VariantInfo {
  fe::Variant variant;
  const char* identifier;  // NEVER change after release: projects reference effects by it
  const char* label;
};

const VariantInfo kVariants[] = {
    {fe::Variant::Combined, "com.venostavisuals.FilmEffects", "Film Effects"},
    {fe::Variant::Bloom, "com.venostavisuals.FilmBloom", "Film Bloom"},
    {fe::Variant::Halation, "com.venostavisuals.FilmHalation", "Film Halation"},
    {fe::Variant::Grain, "com.venostavisuals.FilmGrain", "Film Grain"},
};

/** Stable OFX parameter name: derived from the on-disk ID shared with Premiere (append only). */
std::string paramName(const fe::ParamSpec& s) { return "fe" + std::to_string(s.diskId); }

std::vector<std::string> splitItems(const char* items) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* c = items; c && *c; ++c) {
    if (*c == '|') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += *c;
    }
  }
  out.push_back(cur);
  return out;
}

// ---- instance ---------------------------------------------------------------------------------

class FilmEffectsOFX : public OFX::ImageEffect {
 public:
  FilmEffectsOFX(OfxImageEffectHandle handle, fe::Variant variant)
      : OFX::ImageEffect(handle), variant_(variant), keys_(fe::variantKeys(variant)) {
    src_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    dst_ = fetchClip(kOfxImageEffectOutputClipName);
    params_.resize(keys_.size(), nullptr);
    for (size_t i = 0; i < keys_.size(); ++i) {
      const fe::ParamSpec& s = fe::specFor(keys_[i]);
      const std::string name = paramName(s);
      switch (s.kind) {
        case fe::ParamKind::Checkbox: params_[i] = fetchBooleanParam(name); break;
        case fe::ParamKind::Float: params_[i] = fetchDoubleParam(name); break;
        case fe::ParamKind::Popup: params_[i] = fetchChoiceParam(name); break;
        case fe::ParamKind::IntSlider: params_[i] = fetchIntParam(name); break;
        default: break;  // groups carry no value
      }
    }
  }

  // Host-neutral reader over the OFX parameters at one time (indices as in FilmEffectsParams.h).
  class Reader : public fe::ParamReader {
   public:
    Reader(const FilmEffectsOFX& fx, double t) : fx_(fx), t_(t) {}
    double number(int index) const override {
      OFX::Param* p = fx_.param(index);
      if (auto* d = dynamic_cast<OFX::DoubleParam*>(p)) return d->getValueAtTime(t_);
      if (auto* n = dynamic_cast<OFX::IntParam*>(p)) return n->getValueAtTime(t_);
      return 0.0;
    }
    bool flag(int index) const override {
      auto* b = dynamic_cast<OFX::BooleanParam*>(fx_.param(index));
      return b && b->getValueAtTime(t_);
    }
    int popup(int index) const override {
      auto* c = dynamic_cast<OFX::ChoiceParam*>(fx_.param(index));
      int v = 0;
      if (c) c->getValueAtTime(t_, v);
      return v + 1;  // OFX choices are 0-based, the table is 1-based
    }

   private:
    const FilmEffectsOFX& fx_;
    double t_;
  };

  OFX::Param* param(int hostIndex) const {
    const size_t i = static_cast<size_t>(hostIndex - 1);
    return i < params_.size() ? params_[i] : nullptr;
  }

  fe::FilmParams paramsAt(double t) const { return fe::readParams(Reader(*this, t), variant_); }

  bool isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identityClip, double& identityTime) override {
    if (paramsAt(args.time).anyEffectActive()) return false;
    identityClip = src_;  // bypass: the host passes the source through untouched
    identityTime = args.time;
    return true;
  }

  void changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) override {
    if (variant_ != fe::Variant::Combined || args.reason != OFX::eChangeUserEdit) return;
    if (name == paramName(fe::specFor(fe::ParamKey::Preset))) applyPreset();
  }

  void render(const OFX::RenderArguments& args) override {
    std::unique_ptr<OFX::Image> dst(dst_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> src(src_->fetchImage(args.time));
    if (!dst || !src) OFX::throwSuiteStatusException(kOfxStatFailed);
    if (dst->getPixelDepth() != OFX::eBitDepthFloat || dst->getPixelComponents() != OFX::ePixelComponentRGBA ||
        src->getPixelDepth() != OFX::eBitDepthFloat || src->getPixelComponents() != OFX::ePixelComponentRGBA) {
      OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    const OfxRectI sb = src->getBounds();
    const int w = sb.x2 - sb.x1, h = sb.y2 - sb.y1;
    const fe::FilmParams p = paramsAt(args.time);
    // OFX time is in frames: exact frame index for whole frames, distinct key otherwise.
    const int64_t frameKey = fe::frameKeyFromTime(static_cast<int64_t>(std::llround(args.time * 1000.0)), 1000);

    if (args.isEnabledCudaRender || args.isEnabledMetalRender) {
      renderGpu(args, *src, *dst, p, frameKey);
      return;
    }

    // Source rows into the core's top-row-first RGBA image (OFX rows go bottom to top).
    fe::Image img(std::max(w, 0), std::max(h, 0));
    for (int r = 0; r < h; ++r) {
      const float* s = static_cast<const float*>(src->getPixelAddress(sb.x1, sb.y2 - 1 - r));
      if (s) std::memcpy(img.at(0, r), s, sizeof(float) * 4 * size_t(w));
    }

    if (p.anyEffectActive() && !img.empty()) {
      fe::RenderContext ctx;
      ctx.frameKey = frameKey;
      fe::process(img, p, ctx);  // alpha is never modified
    }

    const OfxRectI rw = args.renderWindow;
    for (int y = rw.y1; y < rw.y2; ++y) {
      float* d = static_cast<float*>(dst->getPixelAddress(rw.x1, y));
      if (!d) continue;
      const int r = sb.y2 - 1 - y;
      for (int x = rw.x1; x < rw.x2; ++x, d += 4) {
        if (r < 0 || r >= h || x < sb.x1 || x >= sb.x2) {
          d[0] = d[1] = d[2] = d[3] = 0.0f;  // outside the source: transparent black
          continue;
        }
        const float* s = img.at(x - sb.x1, r);
        const float* a = static_cast<const float*>(src->getPixelAddress(x, y));
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = a ? a[3] : s[3];  // bit-exact source alpha
      }
    }
  }

 private:
  // GPU frames: the whole image as one buffer (no tiles), RGBA float, bottom row first.
  void renderGpu(const OFX::RenderArguments& args, OFX::Image& src, OFX::Image& dst, const fe::FilmParams& p,
                 int64_t frameKey) {
    const OfxRectI sb = src.getBounds(), db = dst.getBounds();
    const int w = sb.x2 - sb.x1, h = sb.y2 - sb.y1;
    const int rowBytes = src.getRowBytes();
    if (w <= 0 || h <= 0 || sb.x1 != db.x1 || sb.y1 != db.y1 || sb.x2 != db.x2 || sb.y2 != db.y2 ||
        rowBytes != dst.getRowBytes() || rowBytes <= 0 || rowBytes % 16 != 0) {
      OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
    std::lock_guard<std::mutex> lock(gpuMutex_);  // engines keep per-size scratch buffers
    std::unique_ptr<fe::ofx::GpuRenderer>& r = args.isEnabledMetalRender ? metal_ : cuda_;
    if (!r) r = args.isEnabledMetalRender ? fe::ofx::makeMetalRenderer() : fe::ofx::makeCudaRenderer();
    void* queue = args.isEnabledMetalRender ? args.pMetalCmdQ : args.pCudaStream;
    if (!r || !r->render(queue, src.getPixelData(), dst.getPixelData(), uint32_t(w), uint32_t(h),
                         uint32_t(rowBytes / 16), p, frameKey)) {
      OFX::throwSuiteStatusException(kOfxStatFailed);
    }
  }

  std::mutex gpuMutex_;
  std::unique_ptr<fe::ofx::GpuRenderer> cuda_, metal_;
  fe::Variant variant_;
  std::vector<fe::ParamKey> keys_;
  std::vector<OFX::Param*> params_;
  OFX::Clip* src_ = nullptr;
  OFX::Clip* dst_ = nullptr;

  template <typename T>
  T* typed(fe::ParamKey k) const {
    const int i = fe::paramIndex(variant_, k);
    return i < 0 ? nullptr : dynamic_cast<T*>(param(i));
  }

  // Applying a preset writes its values into the normal parameters (keyframeable, saved with the
  // project), then resets the menu to "(choose to apply)" — same behaviour as in Premiere.
  void applyPreset() {
    using K = fe::ParamKey;
    auto* menu = typed<OFX::ChoiceParam>(K::Preset);
    if (!menu) return;
    int choice = 0;
    menu->getValue(choice);  // 0 = "(choose to apply)"
    const char* name = fe::presetMenuName(choice + 1);
    if (!name || choice < 1) return;
    const fe::Preset* preset = fe::findPreset(name);
    if (!preset) return;
    beginEditBlock("Apply Film Effects preset");
    fe::forEachPresetValue(preset->params, [&](K k, double v) {
      switch (fe::specFor(k).kind) {
        case fe::ParamKind::Checkbox: if (auto* b = typed<OFX::BooleanParam>(k)) b->setValue(v != 0.0); break;
        case fe::ParamKind::Popup: if (auto* c = typed<OFX::ChoiceParam>(k)) c->setValue(static_cast<int>(v) - 1); break;
        case fe::ParamKind::IntSlider: if (auto* n = typed<OFX::IntParam>(k)) n->setValue(static_cast<int>(v)); break;
        case fe::ParamKind::Float: if (auto* d = typed<OFX::DoubleParam>(k)) d->setValue(v); break;
        default: break;
      }
    });
    menu->setValue(0);
    endEditBlock();
  }
};

// ---- factory ----------------------------------------------------------------------------------

template <int V>
class FilmEffectsFactory : public OFX::PluginFactoryHelper<FilmEffectsFactory<V>> {
 public:
  FilmEffectsFactory()
      : OFX::PluginFactoryHelper<FilmEffectsFactory<V>>(kVariants[V].identifier, kVersionMajor, kVersionMinor) {}

  void describe(OFX::ImageEffectDescriptor& desc) override {
    desc.setLabels(kVariants[V].label, kVariants[V].label, kVariants[V].label);
    desc.setPluginGrouping(kGrouping);
    desc.setPluginDescription(
        "Bloom, Halation and Film Grain by Venosta Visuals. Free, no warranty. https://venostavisuals.com");
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(false);
    desc.setSupportsTiles(false);  // blurs need the whole frame
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderFullySafe);  // GPU engines are guarded per instance
#ifdef FE_OFX_CUDA
    desc.setSupportsCudaRender(true);  // default stream, finished on return (no CUDA streams)
#endif
#ifdef __APPLE__
    desc.setSupportsMetalRender(true);
#endif
  }

  void describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum) override {
    OFX::ClipDescriptor* src = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    src->addSupportedComponent(OFX::ePixelComponentRGBA);
    src->setTemporalClipAccess(false);
    src->setSupportsTiles(false);
    src->setIsMask(false);
    OFX::ClipDescriptor* dst = desc.defineClip(kOfxImageEffectOutputClipName);
    dst->addSupportedComponent(OFX::ePixelComponentRGBA);
    dst->setSupportsTiles(false);

    OFX::PageParamDescriptor* page = desc.definePageParam("Controls");
    OFX::GroupParamDescriptor* group = nullptr;
    for (fe::ParamKey key : fe::variantKeys(kVariants[V].variant)) {
      const fe::ParamSpec& s = fe::specFor(key);
      const std::string name = paramName(s);
      OFX::ParamDescriptor* d = nullptr;
      switch (s.kind) {
        case fe::ParamKind::GroupStart: {
          group = desc.defineGroupParam(name);
          group->setLabels(s.name, s.name, s.name);
          group->setOpen(!s.startCollapsed);
          if (page) page->addChild(*group);
          continue;
        }
        case fe::ParamKind::GroupEnd: group = nullptr; continue;
        case fe::ParamKind::Checkbox: {
          auto* b = desc.defineBooleanParam(name);
          b->setDefault(s.dflt != 0);
          d = b;
          break;
        }
        case fe::ParamKind::Float: {
          auto* f = desc.defineDoubleParam(name);
          f->setRange(s.validMin, s.validMax);
          f->setDisplayRange(s.sliderMin, s.sliderMax);
          f->setDefault(s.dflt);
          f->setDigits(s.precision);
          f->setIncrement(s.precision >= 2 ? 0.01 : 0.1);
          f->setDoubleType(OFX::eDoubleTypePlain);
          d = f;
          break;
        }
        case fe::ParamKind::Popup: {
          auto* c = desc.defineChoiceParam(name);
          for (const std::string& item : splitItems(s.items)) c->appendOption(item);
          c->setDefault(static_cast<int>(s.dflt) - 1);
          d = c;
          break;
        }
        case fe::ParamKind::IntSlider: {
          auto* n = desc.defineIntParam(name);
          n->setRange(static_cast<int>(s.validMin), static_cast<int>(s.validMax));
          n->setDisplayRange(static_cast<int>(s.sliderMin), static_cast<int>(s.sliderMax));
          n->setDefault(static_cast<int>(s.dflt));
          d = n;
          break;
        }
      }
      if (!d) continue;
      d->setLabels(s.name, s.name, s.name);
      if (s.cannotTimeVary)
        if (auto* v = dynamic_cast<OFX::ValueParamDescriptor*>(d)) v->setAnimates(false);
      if (group) d->setParent(*group);
      else if (page) page->addChild(*d);
    }
  }

  OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum) override {
    return new FilmEffectsOFX(handle, kVariants[V].variant);
  }
};

}  // namespace

namespace OFX {
namespace Plugin {
void getPluginIDs(OFX::PluginFactoryArray& ids) {
  // One effect with every tool. (Standalone Film Bloom / Halation / Grain existed until v1.0.11.)
  static FilmEffectsFactory<0> combined;
  ids.push_back(&combined);
}
}  // namespace Plugin
}  // namespace OFX
