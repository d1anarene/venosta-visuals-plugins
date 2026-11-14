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

  void dnPrepare(const void* s, bool half, Buf opp, const FrameLayout& f) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)s offset:0 atIndex:0];
    [encoder() setBuffer:opp offset:0 atIndex:1];
    [encoder() setBytes:&f length:sizeof(f) atIndex:2];
    dispatch(half ? "dn_prepare_f16" : "dn_prepare_f32", f.width, f.height);
  }
  static const void* ptr(Buf b) { return (__bridge const void*)b; }
  static void* mptr(Buf b) { return (__bridge void*)b; }
  void dnHalve(const void* s, bool half, Buf d, const FrameLayout& f, PlaneInfo hp) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)s offset:0 atIndex:0];
    [encoder() setBuffer:d offset:0 atIndex:1];
    [encoder() setBytes:&f length:sizeof(f) atIndex:2];
    [encoder() setBytes:&hp length:sizeof(hp) atIndex:3];
    dispatch(half ? "dn_halve_f16" : "dn_halve_f32", hp.width, hp.height);
  }
  void dnUpFinish(const void* s, void* d, bool half, Buf low, const FrameLayout& f, PlaneInfo hp) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)s offset:0 atIndex:0];
    [encoder() setBuffer:(__bridge id<MTLBuffer>)d offset:0 atIndex:1];
    [encoder() setBuffer:low offset:0 atIndex:2];
    [encoder() setBytes:&f length:sizeof(f) atIndex:3];
    [encoder() setBytes:&hp length:sizeof(hp) atIndex:4];
    dispatch(half ? "dn_upfinish_f16" : "dn_upfinish_f32", f.width, f.height);
  }
  void dnCrop(const void* s, bool half, Buf rgb, const CropInfo& c) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)s offset:0 atIndex:0];
    [encoder() setBuffer:rgb offset:0 atIndex:1];
    [encoder() setBytes:&c length:sizeof(c) atIndex:2];
    dispatch(half ? "dn_crop_f16" : "dn_crop_f32", c.width, c.height);
  }
  void dnStarletV(Buf s, Buf d, PlaneInfo p, int step) {
    const int st = step;
    [encoder() setBuffer:s offset:0 atIndex:0];
    [encoder() setBuffer:d offset:0 atIndex:1];
    [encoder() setBytes:&p length:sizeof(p) atIndex:2];
    [encoder() setBytes:&st length:sizeof(st) atIndex:3];
    dispatch("dn_starlet_v", p.width, p.height);
  }
  void dnBlocks(Buf c, Buf n, Buf b, size_t off, const BlockGrid& g) {
    [encoder() setBuffer:c offset:0 atIndex:0];
    [encoder() setBuffer:n offset:0 atIndex:1];
    [encoder() setBuffer:b offset:off * sizeof(F4) atIndex:2];
    [encoder() setBytes:&g length:sizeof(g) atIndex:3];
    dispatch("dn_blocks", g.cols, g.rows);
  }
  void dnStarletH(Buf s, Buf t, PlaneInfo p, int step) {
    const int st = step;
    [encoder() setBuffer:s offset:0 atIndex:0];
    [encoder() setBuffer:t offset:0 atIndex:1];
    [encoder() setBytes:&p length:sizeof(p) atIndex:2];
    [encoder() setBytes:&st length:sizeof(st) atIndex:3];
    dispatch("dn_starlet_h", p.width, p.height);
  }
  void dnStarletVBand(Buf t, Buf c, Buf n, Buf b, Buf e, Buf v, const DnProfileTable& tab, const BandInfo& bi, int step) {
    const int st = step;
    [encoder() setBuffer:t offset:0 atIndex:0];
    [encoder() setBuffer:c offset:0 atIndex:1];
    [encoder() setBuffer:n offset:0 atIndex:2];
    [encoder() setBuffer:b offset:0 atIndex:3];
    [encoder() setBuffer:e offset:0 atIndex:4];
    [encoder() setBuffer:v offset:0 atIndex:5];
    [encoder() setBytes:&tab length:sizeof(tab) atIndex:6];
    [encoder() setBytes:&bi length:sizeof(bi) atIndex:7];
    [encoder() setBytes:&st length:sizeof(st) atIndex:8];
    dispatch("dn_starlet_v_band", bi.width, bi.height);
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
  void dnFinish(const void* s, void* d, bool half, Buf out, Buf res, const FrameLayout& f) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)s offset:0 atIndex:0];
    [encoder() setBuffer:(__bridge id<MTLBuffer>)d offset:0 atIndex:1];
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
      "dn_prepare_f32", "dn_prepare_f16", "dn_starlet_h", "dn_starlet_v_band", "dn_pilot",
      "dn_accumulate", "dn_finish_f32", "dn_finish_f16", "dn_crop_f32", "dn_crop_f16", "dn_starlet_v", "dn_blocks",
      "dn_halve_f32", "dn_halve_f16", "dn_upfinish_f32", "dn_upfinish_f16"});
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
                           int view, float mix, bool preview) {
  if (!ok()) return false;
  Backend& B = *impl_->backend;
  B.src = (__bridge const void*)src;
  B.dst = (__bridge void*)dst;
  B.begin(cb);
  const bool done = impl_->pipeline->run(B.src, B.dst, half, w, h, rowPixels, profile, params, rgba, view, mix, preview);
  B.end();
  return done;
}

bool DnMetalEngine::estimateBlocks(id<MTLCommandQueue> queue, id<MTLBuffer> src, bool half, uint32_t w, uint32_t h,
                                   uint32_t rowPixels, bool rgba, bool bottomUp, uint32_t maxW, uint32_t maxH,
                                   std::vector<std::vector<EstimateBlock>>& out, int& cropW, int& cropH, bool preview) {
  if (!ok() || !queue) return false;
  Backend& B = *impl_->backend;
  gpu::SpatialPipeline<Backend>& P = *impl_->pipeline;
  B.src = (__bridge const void*)src;
  id<MTLCommandBuffer> cb = [queue commandBuffer];
  B.begin(cb);
  const bool encoded = P.encodeEstimate(B.src, half, w, h, rowPixels, rgba, bottomUp, maxW, maxH, preview);
  B.end();
  if (!encoded) return false;
  const size_t bytes = size_t(P.blockCount()) * 3 * DN_BANDS * sizeof(vv::gpu::F4);
  id<MTLBuffer> host = [queue.device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
  id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
  [blit copyFromBuffer:P.blocks() sourceOffset:0 toBuffer:host destinationOffset:0 size:bytes];
  [blit endEncoding];
  [cb commit];
  [cb waitUntilCompleted];
  if (cb.status != MTLCommandBufferStatusCompleted) return false;
  out = gpu::unpackBlocks(static_cast<const vv::gpu::F4*>(host.contents), P.blockCount(), DN_BANDS);
  cropW = int(P.cropWidth());
  cropH = int(P.cropHeight());
  return true;
}

bool DnMetalEngine::readRegion(id<MTLCommandQueue> queue, id<MTLBuffer> buf, bool half, uint32_t rowPixels, bool rgba,
                               uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, vv::Image& out) {
  if (!queue || !buf || !w || !h) return false;
  const size_t px = half ? 8 : 16, rowBytes = size_t(w) * px;
  id<MTLBuffer> host = [queue.device newBufferWithLength:rowBytes * h options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> cb = [queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
  for (uint32_t y = 0; y < h; ++y)
    [blit copyFromBuffer:buf sourceOffset:(size_t(y0 + y) * rowPixels + x0) * px toBuffer:host
       destinationOffset:size_t(y) * rowBytes size:rowBytes];
  [blit endEncoding];
  [cb commit];
  [cb waitUntilCompleted];
  if (cb.status != MTLCommandBufferStatusCompleted) return false;
  out = vv::Image(int(w), int(h));
  for (uint32_t i = 0; i < w * h; ++i) {
    const vv::gpu::F4 s = half ? vv::gpu::loadPx(static_cast<const vv::gpu::H4*>(host.contents), i)
                               : vv::gpu::loadPx(static_cast<const vv::gpu::F4*>(host.contents), i);
    float* d = &out.px[size_t(i) * 4];
    d[0] = rgba ? s.x : s.z; d[1] = s.y; d[2] = rgba ? s.z : s.x; d[3] = s.w;
  }
  return true;
}

bool DnMetalEngine::writeRegion(id<MTLCommandQueue> queue, id<MTLBuffer> buf, bool half, uint32_t rowPixels, bool rgba,
                                uint32_t x0, uint32_t y0, const vv::Image& in) {
  if (!queue || !buf || in.width <= 0 || in.height <= 0) return false;
  const uint32_t w = uint32_t(in.width), h = uint32_t(in.height);
  const size_t px = half ? 8 : 16, rowBytes = size_t(w) * px;
  id<MTLBuffer> host = [queue.device newBufferWithLength:rowBytes * h options:MTLResourceStorageModeShared];
  for (uint32_t i = 0; i < w * h; ++i) {
    const float* s = &in.px[size_t(i) * 4];
    const vv::gpu::F4 v = rgba ? vv::gpu::F4{s[0], s[1], s[2], s[3]} : vv::gpu::F4{s[2], s[1], s[0], s[3]};
    if (half) vv::gpu::storePx(static_cast<vv::gpu::H4*>(host.contents), i, v);
    else vv::gpu::storePx(static_cast<vv::gpu::F4*>(host.contents), i, v);
  }
  id<MTLCommandBuffer> cb = [queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
  for (uint32_t y = 0; y < h; ++y)
    [blit copyFromBuffer:host sourceOffset:size_t(y) * rowBytes toBuffer:buf
       destinationOffset:(size_t(y0 + y) * rowPixels + x0) * px size:rowBytes];
  [blit endEncoding];
  [cb commit];
  [cb waitUntilCompleted];
  return cb.status == MTLCommandBufferStatusCompleted;
}
}  // namespace dn
