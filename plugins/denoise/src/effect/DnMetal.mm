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
    const double t0 = nowMs();
    std::vector<std::vector<EstimateBlock>> blocks;
    int cw = 0, ch = 0;
    if (!engine_->estimateBlocks(queue, src, f.half, f.width, f.height, f.rowPixels, f.rgba, f.bottomUp, cropW(ep),
                                 cropH(ep), blocks, cw, ch))
      return false;
    const NoiseProfile prof = profileFromBlocks(blocks, ep.noiseLevel);
    const double t1 = nowMs();
    const bool timing = ep.view == 3;
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    if (!engine_->encode(cb, src, dst, f.half, f.width, f.height, f.rowPixels, prof, spatialParams(ep), f.rgba,
                         timing ? 0 : ep.view, ep.mix))
      return false;
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) return false;
    if (!timing) return true;
    const double t2 = nowMs();
    const Timing tm{"METAL", int(f.width), int(f.height), cw, ch, f.half, ep.preview,
                    t1 - t0, t2 - t1, t2 - t0};
    int bw = 0, bh = 0;
    timingBox(tm, int(f.width), int(f.height), bw, bh);
    const uint32_t y0 = f.bottomUp ? f.height - uint32_t(bh) : 0;
    vv::Image box;
    if (!engine_->readRegion(queue, dst, f.half, f.rowPixels, f.rgba, 0, y0, uint32_t(bw), uint32_t(bh), box))
      return false;
    if (f.bottomUp) flipRows(box);
    drawTiming(box, tm, int(f.height));
    if (f.bottomUp) flipRows(box);
    return engine_->writeRegion(queue, dst, f.half, f.rowPixels, f.rgba, 0, y0, box);
  }

 private:
  id<MTLDevice> device_ = nil;
  std::unique_ptr<DnMetalEngine> engine_;
};

}  // namespace

std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return std::make_unique<MetalRenderer>(); }

}  // namespace dn
