// Metal renderer (macOS) for Premiere Pro and OpenFX hosts. Uses FilmEffects.metallib from the
// bundle's Contents/Resources folder (both host bundles keep the binary in Contents/MacOS).
// STATUS: compiled in CI; the Premiere path was user-tested (macOS), the OpenFX path user-tested in
// DaVinci Resolve (macOS) before this file was shared between them.
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <string>

#include "FilmEffectsGpu.h"
#include "FilmEffectsParams.h"
#include "MetalFilmEngine.h"

namespace fe {
namespace {

std::string metallibPath() {
  Dl_info info;
  if (dladdr(reinterpret_cast<const void*>(&metallibPath), &info) && info.dli_fname) {
    std::string bin = info.dli_fname;  // .../<Bundle>/Contents/MacOS/<binary>
    auto pos = bin.rfind("/MacOS/");
    if (pos != std::string::npos) return bin.substr(0, pos) + "/Resources/FilmEffects.metallib";
  }
  return "FilmEffects.metallib";
}

class MetalRenderer : public vv::GpuRenderer {
 public:
  // Loads the kernels for the queue's device; fails (-> CPU fallback) if the metallib is missing.
  bool prepare(void* queuePtr) override {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)queuePtr;
    if (!queue) return false;
    if (!engine_ || device_ != queue.device) {
      device_ = queue.device;
      engine_ = std::make_unique<MetalFilmEngine>(device_, metallibPath());
    }
    return engine_->ok();
  }

  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t key) override {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)f.queue;
    if (!prepare(f.queue)) return false;
    const FilmParams p = readParams(r);
    MetalFrame src, dst;
    src.buffer = (__bridge id<MTLBuffer>)const_cast<void*>(f.src);
    dst.buffer = (__bridge id<MTLBuffer>)f.dst;
    src.width = dst.width = f.width;
    src.height = dst.height = f.height;
    src.rowPixels = dst.rowPixels = f.rowPixels;
    src.halfFloat = dst.halfFloat = f.half;
    src.rgba = dst.rgba = f.rgba;
    src.bottomUp = dst.bottomUp = f.bottomUp;
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (!p.anyEffectActive()) {
      if (src.buffer != dst.buffer) {  // bypass: exact copy when not rendering in place
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:src.buffer sourceOffset:0 toBuffer:dst.buffer destinationOffset:0
                        size:size_t(f.rowPixels) * f.height * (f.half ? 8 : 16)];
        [blit endEncoding];
      }
    } else if (!engine_->encode(cb, src, dst, p, key)) {
      return false;
    }
    [cb commit];
    [cb waitUntilCompleted];  // the frame must be finished when render returns
    return cb.status == MTLCommandBufferStatusCompleted;
  }

 private:
  id<MTLDevice> device_ = nil;
  std::unique_ptr<MetalFilmEngine> engine_;
};

}  // namespace

std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return std::make_unique<MetalRenderer>(); }

}  // namespace fe
