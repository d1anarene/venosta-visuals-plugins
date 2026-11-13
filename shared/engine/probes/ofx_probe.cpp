// Research-only temporal access and user-action persistence probe. CPU float RGBA only.
#include "ofxsImageEffect.h"
#include "probe_common.h"
#include <algorithm>
#include <memory>
#include <sstream>

namespace {
constexpr const char* kId = "com.venostavisuals.research.TemporalProbe";
vv::Image read(OFX::Image& image) {
  if (image.getPixelDepth() != OFX::eBitDepthFloat || image.getPixelComponents() != OFX::ePixelComponentRGBA)
    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
  const OfxRectI bounds = image.getBounds();
  vv::Image result(bounds.x2 - bounds.x1, bounds.y2 - bounds.y1);
  for (int y = bounds.y1; y < bounds.y2; ++y) for (int x = bounds.x1; x < bounds.x2; ++x) {
    const float* p = static_cast<const float*>(image.getPixelAddress(x,y));
    if (!p) OFX::throwSuiteStatusException(kOfxStatFailed);
    std::memcpy(result.at(x-bounds.x1, bounds.y2-1-y), p, sizeof(float)*4);
  }
  return result;
}
class Probe : public OFX::ImageEffect {
 public:
  explicit Probe(OfxImageEffectHandle handle) : OFX::ImageEffect(handle) {
    source_ = fetchClip(kOfxImageEffectSimpleSourceClipName); output_ = fetchClip(kOfxImageEffectOutputClipName);
    offset_ = fetchIntParam("offset"); mode_ = fetchIntParam("view"); gain_ = fetchDoubleParam("gain");
    log_ = fetchBooleanParam("log"); mean_ = fetchDoubleParam("measuredMean");
  }
  void getFramesNeeded(const OFX::FramesNeededArguments& args, OFX::FramesNeededSetter& frames) override {
    const int offset = offset_->getValueAtTime(args.time);
    if (offset < -3 || offset > 3 || !std::isfinite(args.time)) OFX::throwSuiteStatusException(kOfxStatErrValue);
    const OfxRangeD range = {std::min(args.time,args.time+offset), std::max(args.time,args.time+offset)};
    frames.setFramesNeeded(*source_, range);
  }
  void changedParam(const OFX::InstanceChangedArgs& args, const std::string& name) override {
    if (args.reason != OFX::eChangeUserEdit || name != "analyse") return;
    // Image lifetime ends in this editing action. Persistent values are NEVER written in render.
    std::unique_ptr<OFX::Image> image(source_->fetchImage(args.time));
    if (!image) OFX::throwSuiteStatusException(kOfxStatFailed);
    const vv::Image current = read(*image);
    const double measured = vv::probe::mean(current);
    beginEditBlock("Measure temporal probe input");
    try { mean_->setValue(measured); } catch (...) { endEditBlock(); throw; }
    endEditBlock();
    if (log_->getValueAtTime(args.time)) {
      std::ostringstream s; s.precision(17);
      s << "{\"schema\":\"" << vv::probe::kSchema << "\",\"host_path\":\"ofx-cpu\",\"action\":\"user-analysis\",\"time\":"
        << args.time << ",\"measured_mean\":" << measured << ',' << vv::probe::imageFields(current,nullptr) << '}';
      vv::probe::log(s.str());
    }
  }
  void render(const OFX::RenderArguments& args) override {
    const int offset = offset_->getValueAtTime(args.time), mode = mode_->getValueAtTime(args.time);
    if (offset < -3 || offset > 3 || mode < 0 || mode > 2 || !std::isfinite(args.time))
      OFX::throwSuiteStatusException(kOfxStatErrValue);
    const bool logging = log_->getValueAtTime(args.time);
    std::unique_ptr<OFX::Image> currentImage(source_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> neighbourImage(source_->fetchImage(args.time+offset));
    std::unique_ptr<OFX::Image> target(output_->fetchImage(args.time));
    if (!currentImage || !target) OFX::throwSuiteStatusException(kOfxStatFailed);
    if (target->getPixelDepth()!=OFX::eBitDepthFloat || target->getPixelComponents()!=OFX::ePixelComponentRGBA)
      OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    const vv::Image current = read(*currentImage);
    vv::Image neighbour; const vv::Image* other = nullptr;
    if (neighbourImage) { neighbour = read(*neighbourImage); other = &neighbour; }
    const OfxRectI cb = currentImage->getBounds();
    const OfxRectI nb = neighbourImage ? neighbourImage->getBounds() : OfxRectI{};
    const bool sameBounds = other && cb.x1==nb.x1 && cb.y1==nb.y1 && cb.x2==nb.x2 && cb.y2==nb.y2;
    if (logging) {
      std::ostringstream s; s.precision(17);
      s << "{\"schema\":\"" << vv::probe::kSchema << "\",\"host_path\":\"ofx-cpu\",\"action\":\"render\",\"time\":" << args.time
        << ",\"requested_neighbour_time\":" << args.time+offset
        << ",\"returned_time_verified\":false,\"offset_frames\":" << offset << ",\"view\":" << mode
        << ",\"draft\":" << (args.renderQualityDraft ? "true":"false")
        << ",\"interactive\":" << (args.interactiveRenderStatus ? "true":"false")
        << ",\"scale_x\":" << args.renderScale.x << ",\"scale_y\":" << args.renderScale.y
        << ",\"format\":\"RGBA-float32-bottom-up\",\"bounds_x\":" << cb.x1 << ",\"bounds_y\":" << cb.y1
        << ",\"neighbour_bounds_x\":" << nb.x1 << ",\"neighbour_bounds_y\":" << nb.y1
        << ",\"same_bounds\":" << (sameBounds?"true":"false") << ',' << vv::probe::imageFields(current,other) << '}';
      vv::probe::log(s.str());
    }
    if (mode && !sameBounds) OFX::throwSuiteStatusException(kOfxStatFailed);
    const vv::Image result = vv::probe::display(current, other, mode, gain_->getValueAtTime(args.time));
    const OfxRectI rw = args.renderWindow;
    for (int y=rw.y1; y<rw.y2; ++y) for (int x=rw.x1; x<rw.x2; ++x) {
      float* p = static_cast<float*>(target->getPixelAddress(x,y));
      if (!p) OFX::throwSuiteStatusException(kOfxStatFailed);
      if (x<cb.x1 || x>=cb.x2 || y<cb.y1 || y>=cb.y2) OFX::throwSuiteStatusException(kOfxStatFailed);
      std::memcpy(p,result.at(x-cb.x1,cb.y2-1-y),sizeof(float)*4);
    }
  }
 private:
  OFX::Clip *source_, *output_;
  OFX::IntParam *offset_, *mode_;
  OFX::DoubleParam *gain_, *mean_;
  OFX::BooleanParam* log_;
};
class Factory : public OFX::PluginFactoryHelper<Factory> {
 public:
  Factory(): OFX::PluginFactoryHelper<Factory>(kId,1,0) {}
  void describe(OFX::ImageEffectDescriptor& d) override {
    d.setLabels("Temporal Probe (research)","Temporal Probe (research)","Temporal Probe (research)");
    d.setPluginGrouping("Venosta Visuals Research");
    d.setPluginDescription("Temporal input-stage research only. Not a denoiser. View 0=current, 1=neighbour, 2=absolute difference. Check numbered inputs and logs; no automatic pass verdict.");
    d.addSupportedContext(OFX::eContextFilter); d.addSupportedContext(OFX::eContextGeneral);
    d.addSupportedBitDepth(OFX::eBitDepthFloat); d.setTemporalClipAccess(true);
    d.setSupportsTiles(false); d.setSupportsMultiResolution(false); d.setHostFrameThreading(false);
    d.setRenderThreadSafety(OFX::eRenderFullySafe);
  }
  void describeInContext(OFX::ImageEffectDescriptor& d, OFX::ContextEnum) override {
    auto* source=d.defineClip(kOfxImageEffectSimpleSourceClipName);
    source->addSupportedComponent(OFX::ePixelComponentRGBA); source->setTemporalClipAccess(true); source->setSupportsTiles(false);
    auto* out=d.defineClip(kOfxImageEffectOutputClipName); out->addSupportedComponent(OFX::ePixelComponentRGBA); out->setSupportsTiles(false);
    auto* offset=d.defineIntParam("offset"); offset->setLabel("Neighbour offset (frames)"); offset->setRange(-3,3); offset->setDisplayRange(-3,3); offset->setDefault(-1);
    auto* mode=d.defineIntParam("view"); mode->setLabel("View: 0 current / 1 neighbour / 2 difference"); mode->setRange(0,2); mode->setDisplayRange(0,2); mode->setDefault(0);
    auto* gain=d.defineDoubleParam("gain"); gain->setLabel("Difference gain"); gain->setRange(0,1000); gain->setDisplayRange(0,10); gain->setDefault(1);
    auto* log=d.defineBooleanParam("log"); log->setLabel("Log diagnostics"); log->setDefault(false); log->setAnimates(false);
    auto* action=d.definePushButtonParam("analyse"); action->setLabel("Measure current input");
    auto* mean=d.defineDoubleParam("measuredMean"); mean->setLabel("Saved measured RGB mean"); mean->setDefault(0); mean->setRange(-1e6,1e6); mean->setAnimates(false); mean->setEvaluateOnChange(false);
  }
  OFX::ImageEffect* createInstance(OfxImageEffectHandle h, OFX::ContextEnum) override { return new Probe(h); }
};
}
namespace OFX { namespace Plugin { void getPluginIDs(OFX::PluginFactoryArray& ids) { static Factory factory; ids.push_back(&factory); } } }
