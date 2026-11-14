#import "DnMetalEngine.h"

#include <algorithm>

#include "dn_gpu_pipeline.h"
#include "gpu/metal/vv_metal_backend.h"

namespace dn {
namespace {
using namespace dn::gpu;

struct Backend : vv::gpu::MetalBackend {
  using vv::gpu::MetalBackend::MetalBackend;
  const void* src = nullptr;  // current frame (id<MTLBuffer> bridged)
  void* dst = nullptr;
  id<MTLBuffer> one = nil;    // placeholder for the unused noise-scale buffer

  void dnPrepare(const void*, bool half, Buf opp, Buf rgb, const FrameLayout& f) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)src offset:0 atIndex:0];
    [encoder() setBuffer:opp offset:0 atIndex:1];
    [encoder() setBuffer:rgb offset:0 atIndex:2];
    [encoder() setBytes:&f length:sizeof(f) atIndex:3];
    dispatch(half ? "dn_prepare_f16" : "dn_prepare_f32", f.width, f.height);
  }
  void dnStarlet(Buf s, Buf t, Buf d, PlaneInfo p, int step) {
    const int st = step;
    [encoder() setBuffer:s offset:0 atIndex:0];
    [encoder() setBuffer:t offset:0 atIndex:1];
    [encoder() setBytes:&p length:sizeof(p) atIndex:2];
    [encoder() setBytes:&st length:sizeof(st) atIndex:3];
    dispatch("dn_starlet_h", p.width, p.height);
    [encoder() setBuffer:t offset:0 atIndex:0];
    [encoder() setBuffer:d offset:0 atIndex:1];
    dispatch("dn_starlet_v", p.width, p.height);
  }
  void dnBand(Buf c, Buf n, Buf b, Buf e, PlaneInfo p) {
    [encoder() setBuffer:c offset:0 atIndex:0];
    [encoder() setBuffer:n offset:0 atIndex:1];
    [encoder() setBuffer:b offset:0 atIndex:2];
    [encoder() setBuffer:e offset:0 atIndex:3];
    [encoder() setBytes:&p length:sizeof(p) atIndex:4];
    dispatch("dn_band", p.width, p.height);
  }
  void dnVar(Buf low, Buf scale, Buf var, const DnProfileTable& t, const BandInfo& b) {
    [encoder() setBuffer:low offset:0 atIndex:0];
    [encoder() setBuffer:(scale ? scale : low) offset:0 atIndex:1];  // unused when hasScale == 0
    [encoder() setBuffer:var offset:0 atIndex:2];
    [encoder() setBytes:&t length:sizeof(t) atIndex:3];
    [encoder() setBytes:&b length:sizeof(b) atIndex:4];
    dispatch("dn_var", b.width, b.height);
  }
  void dnPilot(Buf band, Buf E, Buf var, Buf p2, const BandInfo& b) {
    [encoder() setBuffer:band offset:0 atIndex:0];
    [encoder() setBuffer:E offset:0 atIndex:1];
    [encoder() setBuffer:var offset:0 atIndex:2];
    [encoder() setBuffer:p2 offset:0 atIndex:3];
    [encoder() setBytes:&b length:sizeof(b) atIndex:4];
    dispatch("dn_pilot", b.width, b.height);
  }
  void dnAccumulate(Buf out, Buf band, Buf P, Buf var, const BandInfo& b) {
    [encoder() setBuffer:out offset:0 atIndex:0];
    [encoder() setBuffer:band offset:0 atIndex:1];
    [encoder() setBuffer:P offset:0 atIndex:2];
    [encoder() setBuffer:var offset:0 atIndex:3];
    [encoder() setBytes:&b length:sizeof(b) atIndex:4];
    dispatch("dn_accumulate", b.width, b.height);
  }
  void dnFinish(const void*, void*, bool half, Buf out, Buf res, const FrameLayout& f) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)src offset:0 atIndex:0];
    [encoder() setBuffer:(__bridge id<MTLBuffer>)dst offset:0 atIndex:1];
    [encoder() setBuffer:out offset:0 atIndex:2];
    [encoder() setBuffer:res offset:0 atIndex:3];
    [encoder() setBytes:&f length:sizeof(f) atIndex:4];
    dispatch(half ? "dn_finish_f16" : "dn_finish_f32", f.width, f.height);
  }
};

}  // namespace

struct DnMetalEngine::Impl {
  std::string error;
  std::unique_ptr<Backend> backend;
  std::unique_ptr<gpu::SpatialPipeline<Backend>> pipeline;
};

DnMetalEngine::DnMetalEngine(id<MTLDevice> device, const std::string& path) : impl_(new Impl) {
  NSError* err = nil;
  id<MTLLibrary> lib = [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]] error:&err];
  if (!lib) {
    impl_->error = "Cannot load " + path;
    return;
  }
  impl_->backend = std::make_unique<Backend>(device, lib, std::vector<std::string>{
      "dn_prepare_f32", "dn_prepare_f16", "dn_starlet_h", "dn_starlet_v", "dn_band", "dn_var", "dn_pilot",
      "dn_accumulate", "dn_finish_f32", "dn_finish_f16"});
  if (!impl_->backend->ok()) {
    impl_->error = impl_->backend->error();
    return;
  }
  impl_->pipeline = std::make_unique<gpu::SpatialPipeline<Backend>>(*impl_->backend);
}
DnMetalEngine::~DnMetalEngine() = default;
bool DnMetalEngine::ok() const { return impl_ && impl_->error.empty() && impl_->pipeline; }
const std::string& DnMetalEngine::error() const { return impl_->error; }

bool DnMetalEngine::encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t w, uint32_t h,
                           uint32_t rowPixels, const NoiseProfile& profile, const SpatialParams& params, bool rgba,
                           int view, float mix) {
  if (!ok()) return false;
  Backend& B = *impl_->backend;
  B.src = (__bridge const void*)src;
  B.dst = (__bridge void*)dst;
  B.begin(cb);
  const bool done = impl_->pipeline->run(B.src, B.dst, half, w, h, rowPixels, profile, params, rgba, view, mix);
  B.end();
  return done;
}

bool DnMetalEngine::downloadCrop(id<MTLCommandQueue> queue, id<MTLBuffer> src, bool half, uint32_t w, uint32_t h,
                                 uint32_t rowPixels, bool rgba, bool bottomUp, uint32_t maxW, uint32_t maxH, vv::Image& out) {
  if (!queue || !src || !w || !h) return false;
  const uint32_t cw = std::min(w, maxW), ch = std::min(h, maxH), x0 = (w - cw) / 2, y0 = (h - ch) / 2;
  const size_t px = half ? 8 : 16, rowBytes = size_t(cw) * px;
  id<MTLBuffer> host = [queue.device newBufferWithLength:rowBytes * ch options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> cb = [queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
  for (uint32_t y = 0; y < ch; ++y)
    [blit copyFromBuffer:src sourceOffset:(size_t(y0 + y) * rowPixels + x0) * px toBuffer:host
       destinationOffset:size_t(y) * rowBytes size:rowBytes];
  [blit endEncoding];
  [cb commit];
  [cb waitUntilCompleted];
  if (cb.status != MTLCommandBufferStatusCompleted) return false;
  out = vv::Image(int(cw), int(ch));
  for (uint32_t y = 0; y < ch; ++y)
    for (uint32_t x = 0; x < cw; ++x) {
      const size_t i = size_t(y) * cw + x;
      const vv::gpu::F4 s = half ? vv::gpu::loadPx(static_cast<const vv::gpu::H4*>(host.contents), uint32_t(i))
                                 : vv::gpu::loadPx(static_cast<const vv::gpu::F4*>(host.contents), uint32_t(i));
      float* d = out.at(int(x), int(bottomUp ? ch - 1 - y : y));
      d[0] = rgba ? s.x : s.z; d[1] = s.y; d[2] = rgba ? s.z : s.x; d[3] = s.w;
    }
  return true;
}
}  // namespace dn
