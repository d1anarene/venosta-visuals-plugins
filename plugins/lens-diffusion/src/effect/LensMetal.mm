// Metal renderer (macOS) for Premiere Pro and OpenFX hosts. Uses LensDiffusion.metallib from the
// bundle's Contents/Resources folder. STATUS: compiled in CI; not yet run inside a host.
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <string>

#include "LensGpu.h"
#include "LensMetalEngine.h"
#include "LensParamsTable.h"

namespace ld {
namespace {

std::string metallibPath() {
  Dl_info info;
  if (dladdr(reinterpret_cast<const void*>(&metallibPath), &info) && info.dli_fname) {
    std::string bin = info.dli_fname;  // .../<Bundle>/Contents/MacOS/<binary>
    auto pos = bin.rfind("/MacOS/");
    if (pos != std::string::npos) return bin.substr(0, pos) + "/Resources/LensDiffusion.metallib";
  }
  return "LensDiffusion.metallib";
}

class MetalRenderer : public vv::GpuRenderer {
 public:
  bool prepare(void* queuePtr) override {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)queuePtr;
    if (!queue) return false;
    if (!engine_ || device_ != queue.device) {
      device_ = queue.device;
      engine_ = std::make_unique<LensMetalEngine>(device_, metallibPath());
    }
    return engine_->ok();
  }

  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t) override {
    if (!prepare(f.queue)) return false;
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)f.queue;
    const LensParams p = readParams(r);
    id<MTLBuffer> src = (__bridge id<MTLBuffer>)const_cast<void*>(f.src);
    id<MTLBuffer> dst = (__bridge id<MTLBuffer>)f.dst;
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (!p.anyEffectActive()) {
      if (src != dst) {  // bypass: exact copy when not rendering in place
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:src sourceOffset:0 toBuffer:dst destinationOffset:0
                        size:size_t(f.rowPixels) * f.height * (f.half ? 8 : 16)];
        [blit endEncoding];
      }
    } else if (!engine_->encode(cb, src, dst, f.half, f.width, f.height, f.rowPixels, p, f.rgba)) {
      return false;
    }
    [cb commit];
    [cb waitUntilCompleted];
    return cb.status == MTLCommandBufferStatusCompleted;
  }

 private:
  id<MTLDevice> device_ = nil;
  std::unique_ptr<LensMetalEngine> engine_;
};

}  // namespace

std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return std::make_unique<MetalRenderer>(); }

}  // namespace ld
