// Shared OpenFX adapter (DaVinci Resolve and other OFX hosts) for any vv::Effect.
//
// Registers one effect (vv::effect()), creates its parameters from the plugin's table (OFX names
// "<ofxParamPrefix><diskId>", stable across versions), bypasses via isIdentity, renders on the CPU through the
// plugin's render() or on the GPU (CUDA / Metal) through its GpuRenderer when the host enables it.
// Built on the BSD-licensed OpenFX C++ Support library (see vv_ofx.cmake).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "vv/effect.h"

namespace {

const vv::Effect& E() { return vv::effect(); }

/** Stable OFX parameter name from the on-disk ID (append only); saved in host projects. */
std::string paramName(const vv::ParamSpec& s) { return E().info().ofxParamPrefix + std::to_string(s.diskId); }

class Instance : public OFX::ImageEffect {
 public:
  explicit Instance(OfxImageEffectHandle handle) : OFX::ImageEffect(handle) {
    src_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    dst_ = fetchClip(kOfxImageEffectOutputClipName);
    const auto& specs = E().params();
    params_.resize(specs.size(), nullptr);
    for (size_t i = 0; i < specs.size(); ++i) {
      const std::string name = paramName(specs[i]);
      switch (specs[i].kind) {
        case vv::ParamKind::Checkbox: params_[i] = fetchBooleanParam(name); break;
        case vv::ParamKind::Float: params_[i] = fetchDoubleParam(name); break;
        case vv::ParamKind::Popup: params_[i] = fetchChoiceParam(name); break;
        case vv::ParamKind::IntSlider: params_[i] = fetchIntParam(name); break;
        default: break;  // groups carry no value
      }
    }
  }

  OFX::Param* param(int hostIndex) const {
    const size_t i = static_cast<size_t>(hostIndex - 1);
    return i < params_.size() ? params_[i] : nullptr;
  }

  class Reader : public vv::ParamReader {
   public:
    Reader(const Instance& fx, double t) : fx_(fx), t_(t) {}
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
      return v + 1;  // OFX choices are 0-based, the tables are 1-based
    }

   private:
    const Instance& fx_;
    double t_;
  };

  class Writer : public vv::ParamWriter {
   public:
    explicit Writer(Instance& fx) : fx_(fx) {}
    void setNumber(int index, double v) override {
      OFX::Param* p = fx_.param(index);
      if (auto* d = dynamic_cast<OFX::DoubleParam*>(p)) d->setValue(v);
      else if (auto* n = dynamic_cast<OFX::IntParam*>(p)) n->setValue(static_cast<int>(std::lround(v)));
    }
    void setFlag(int index, bool v) override {
      if (auto* b = dynamic_cast<OFX::BooleanParam*>(fx_.param(index))) b->setValue(v);
    }
    void setPopup(int index, int v) override {
      if (auto* c = dynamic_cast<OFX::ChoiceParam*>(fx_.param(index))) c->setValue(v - 1);
    }

   private:
    Instance& fx_;
  };

  bool isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identityClip, double& identityTime) override {
    if (E().active(Reader(*this, args.time))) return false;
    identityClip = src_;  // bypass: the host passes the source through untouched
    identityTime = args.time;
    return true;
  }

  void changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) override {
    if (args.reason != OFX::eChangeUserEdit) return;
    const auto& specs = E().params();
    for (size_t i = 0; i < specs.size(); ++i) {
      if (!specs[i].supervise || paramName(specs[i]) != name) continue;
      Writer w(*this);
      beginEditBlock("Venosta Visuals");
      E().paramChanged(static_cast<int>(i) + 1, Reader(*this, args.time), w);
      endEditBlock();
      return;
    }
  }

  void render(const OFX::RenderArguments& args) override {
    std::unique_ptr<OFX::Image> dst(dst_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> src(src_->fetchImage(args.time));
    if (!dst || !src) OFX::throwSuiteStatusException(kOfxStatFailed);
    if (dst->getPixelDepth() != OFX::eBitDepthFloat || dst->getPixelComponents() != OFX::ePixelComponentRGBA ||
        src->getPixelDepth() != OFX::eBitDepthFloat || src->getPixelComponents() != OFX::ePixelComponentRGBA) {
      OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
    const Reader reader(*this, args.time);
    // OFX time is in frames: exact frame index for whole frames, distinct key otherwise.
    const int64_t frameKey = vv::frameKeyFromTime(static_cast<int64_t>(std::llround(args.time * 1000.0)), 1000);

    if (args.isEnabledCudaRender || args.isEnabledMetalRender) {
      renderGpu(args, *src, *dst, reader, frameKey);
      return;
    }

    const OfxRectI sb = src->getBounds();
    const int w = sb.x2 - sb.x1, h = sb.y2 - sb.y1;
    // Source rows into the engine's top-row-first RGBA image (OFX rows go bottom to top).
    vv::Image img(std::max(w, 0), std::max(h, 0));
    for (int r = 0; r < h; ++r) {
      const float* s = static_cast<const float*>(src->getPixelAddress(sb.x1, sb.y2 - 1 - r));
      if (s) std::memcpy(img.at(0, r), s, sizeof(float) * 4 * size_t(w));
    }
    if (!img.empty() && E().active(reader)) E().render(img, reader, frameKey);  // alpha never modified

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
  void renderGpu(const OFX::RenderArguments& args, OFX::Image& src, OFX::Image& dst, const vv::ParamReader& reader,
                 int64_t frameKey) {
    const OfxRectI sb = src.getBounds(), db = dst.getBounds();
    const int w = sb.x2 - sb.x1, h = sb.y2 - sb.y1;
    const int rowBytes = src.getRowBytes();
    if (w <= 0 || h <= 0 || sb.x1 != db.x1 || sb.y1 != db.y1 || sb.x2 != db.x2 || sb.y2 != db.y2 ||
        rowBytes != dst.getRowBytes() || rowBytes <= 0 || rowBytes % 16 != 0) {
      OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
    const vv::GpuApi api = args.isEnabledMetalRender ? vv::GpuApi::Metal : vv::GpuApi::Cuda;
    std::lock_guard<std::mutex> lock(gpuMutex_);  // renderers keep per-size scratch buffers
    std::unique_ptr<vv::GpuRenderer>& r = api == vv::GpuApi::Metal ? metal_ : cuda_;
    if (!r) r = E().makeGpuRenderer(api);
    vv::GpuFrame f;
    f.src = src.getPixelData();
    f.dst = dst.getPixelData();
    f.width = uint32_t(w);
    f.height = uint32_t(h);
    f.rowPixels = uint32_t(rowBytes / 16);
    f.rgba = true;
    f.bottomUp = true;
    f.queue = api == vv::GpuApi::Metal ? args.pMetalCmdQ : args.pCudaStream;
    if (!r || !r->render(f, reader, frameKey)) OFX::throwSuiteStatusException(kOfxStatFailed);
  }

  std::vector<OFX::Param*> params_;
  OFX::Clip* src_ = nullptr;
  OFX::Clip* dst_ = nullptr;
  std::mutex gpuMutex_;
  std::unique_ptr<vv::GpuRenderer> cuda_, metal_;
};

class Factory : public OFX::PluginFactoryHelper<Factory> {
 public:
  Factory()
      : OFX::PluginFactoryHelper<Factory>(E().info().ofxId, static_cast<unsigned>(E().info().versionMajor),
                                          static_cast<unsigned>(E().info().versionMinor)) {}

  void describe(OFX::ImageEffectDescriptor& desc) override {
    const vv::EffectInfo& info = E().info();
    desc.setLabels(info.name, info.name, info.name);
    desc.setPluginGrouping(info.category);
    desc.setPluginDescription(std::string(info.description) + " Free, no warranty. https://venostavisuals.com");
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(false);
    desc.setSupportsTiles(false);  // effects may need the whole frame (blurs)
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderFullySafe);  // GPU renderers are guarded per instance
#ifndef __APPLE__
    // Default stream, finished on return (no CUDA streams).
    if (E().hasGpu(vv::GpuApi::Cuda)) desc.setSupportsCudaRender(true);
#else
    if (E().hasGpu(vv::GpuApi::Metal)) desc.setSupportsMetalRender(true);
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
    for (const vv::ParamSpec& s : E().params()) {
      const std::string name = paramName(s);
      OFX::ParamDescriptor* d = nullptr;
      switch (s.kind) {
        case vv::ParamKind::GroupStart: {
          group = desc.defineGroupParam(name);
          group->setLabels(s.name, s.name, s.name);
          group->setOpen(!s.startCollapsed);
          if (page) page->addChild(*group);
          continue;
        }
        case vv::ParamKind::GroupEnd: group = nullptr; continue;
        case vv::ParamKind::Checkbox: {
          auto* b = desc.defineBooleanParam(name);
          b->setDefault(s.dflt != 0);
          d = b;
          break;
        }
        case vv::ParamKind::Float: {
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
        case vv::ParamKind::Popup: {
          auto* c = desc.defineChoiceParam(name);
          for (const std::string& item : vv::splitItems(s.items)) c->appendOption(item);
          c->setDefault(static_cast<int>(s.dflt) - 1);
          d = c;
          break;
        }
        case vv::ParamKind::IntSlider: {
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
    return new Instance(handle);
  }
};

}  // namespace

namespace OFX {
namespace Plugin {
void getPluginIDs(OFX::PluginFactoryArray& ids) {
  static Factory factory;
  ids.push_back(&factory);
}
}  // namespace Plugin
}  // namespace OFX
