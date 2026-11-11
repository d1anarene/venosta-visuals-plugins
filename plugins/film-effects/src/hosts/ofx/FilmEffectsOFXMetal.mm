// Metal backend for the OpenFX plug-in (DaVinci Resolve on macOS). Uses the same engine and
// FilmEffects.metallib as the Premiere Metal entry point; the metallib ships in the bundle's
// Contents/Resources folder.
// STATUS: compiled in CI; not yet run inside DaVinci Resolve.
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <string>

#include "FilmEffectsOFXGpu.h"
#include "MetalFilmEngine.h"

namespace fe {
namespace ofx {
namespace {

std::string metallibPath() {
  Dl_info info;
  if (dladdr(reinterpret_cast<const void*>(&metallibPath), &info) && info.dli_fname) {
    std::string bin = info.dli_fname;  // .../FilmEffects.ofx.bundle/Contents/MacOS/FilmEffects.ofx
    auto pos = bin.rfind("/MacOS/");
    if (pos != std::string::npos) return bin.substr(0, pos) + "/Resources/FilmEffects.metallib";
  }
  return "FilmEffects.metallib";
}

class MetalRenderer : public GpuRenderer {
 public:
  bool render(void* queuePtr, const void* srcPtr, void* dstPtr, uint32_t w, uint32_t h, uint32_t rowPixels,
              const FilmParams& p, int64_t key) override {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)queuePtr;
    if (!queue) return false;
    if (!engine_ || device_ != queue.device) {
      device_ = queue.device;
      engine_ = std::make_unique<MetalFilmEngine>(device_, metallibPath());
    }
    if (!engine_->ok()) return false;
    MetalFrame src, dst;
    src.buffer = (__bridge id<MTLBuffer>)const_cast<void*>(srcPtr);
    dst.buffer = (__bridge id<MTLBuffer>)dstPtr;
    src.width = dst.width = w;
    src.height = dst.height = h;
    src.rowPixels = dst.rowPixels = rowPixels;
    src.rgba = dst.rgba = true;
    src.bottomUp = dst.bottomUp = true;
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (!p.anyEffectActive()) {
      if (src.buffer != dst.buffer) {
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:src.buffer sourceOffset:0 toBuffer:dst.buffer destinationOffset:0
                        size:size_t(rowPixels) * h * 16];
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

std::unique_ptr<GpuRenderer> makeMetalRenderer() { return std::make_unique<MetalRenderer>(); }

}  // namespace ofx
}  // namespace fe
