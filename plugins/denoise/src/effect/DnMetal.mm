// Denoise Metal renderer (macOS, spatial filter only). Uses Denoise.metallib from the bundle's
// Contents/Resources. STATUS: compiled in CI; not run in a host.
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <string>

#include "DnGpu.h"
#include "DnMetalEngine.h"

namespace dn {
namespace {

std::string metallibPath() {
  Dl_info info;
  if (dladdr(reinterpret_cast<const void*>(&metallibPath), &info) && info.dli_fname) {
    std::string bin = info.dli_fname;
    auto pos = bin.rfind("/MacOS/");
    if (pos != std::string::npos) return bin.substr(0, pos) + "/Resources/Denoise.metallib";
  }
  return "Denoise.metallib";
}

class MetalRenderer : public vv::GpuRenderer {
 public:
  bool prepare(void* queuePtr) override {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)queuePtr;
    if (!queue) return false;
    if (!engine_ || device_ != queue.device) {
      device_ = queue.device;
      engine_ = std::make_unique<DnMetalEngine>(device_, metallibPath());
    }
    return engine_->ok();
  }
  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t) override {
    if (!prepare(f.queue)) return false;
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)f.queue;
    const EffectParams ep = readParams(r);
    id<MTLBuffer> src = (__bridge id<MTLBuffer>)const_cast<void*>(f.src);
    id<MTLBuffer> dst = (__bridge id<MTLBuffer>)f.dst;
    vv::Image crop;
    if (!engine_->downloadCrop(queue, src, f.half, f.width, f.height, f.rowPixels, f.rgba, f.bottomUp, cropW(ep),
                               cropH(ep), crop))
      return false;
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (!engine_->encode(cb, src, dst, f.half, f.width, f.height, f.rowPixels,
                         estimateSpatialProfile(crop, ep.noiseLevel), spatialParams(ep), f.rgba, ep.view, ep.mix))
      return false;
    [cb commit];
    [cb waitUntilCompleted];
    return cb.status == MTLCommandBufferStatusCompleted;
  }

 private:
  id<MTLDevice> device_ = nil;
  std::unique_ptr<DnMetalEngine> engine_;
};

}  // namespace

std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return std::make_unique<MetalRenderer>(); }

}  // namespace dn
