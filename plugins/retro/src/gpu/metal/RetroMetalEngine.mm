#import "RetroMetalEngine.h"

#include "gpu/metal/vv_metal_backend.h"
#include "rt_gpu_pipeline.h"

namespace rt {
namespace {

using namespace rt::gpu;

struct Backend : vv::gpu::MetalBackend {
  using vv::gpu::MetalBackend::MetalBackend;
  const void* src = nullptr;  // current frame (id<MTLBuffer> bridged)
  void* dst = nullptr;

  void rtPrepare(const void*, bool half, Buf yuv, const FrameLayout& f) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)src offset:0 atIndex:0];
    [encoder() setBuffer:yuv offset:0 atIndex:1];
    [encoder() setBytes:&f length:sizeof(f) atIndex:2];
    dispatch(half ? "rt_prepare_f16" : "rt_prepare_f32", f.width, f.height);
  }
  void rtComposite(const void*, void*, bool half, Buf l, Buf r, Buf c, const FrameLayout& f, const RtInfo& info) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)src offset:0 atIndex:0];
    [encoder() setBuffer:(__bridge id<MTLBuffer>)dst offset:0 atIndex:1];
    [encoder() setBuffer:l offset:0 atIndex:2];
    [encoder() setBuffer:r offset:0 atIndex:3];
    [encoder() setBuffer:c offset:0 atIndex:4];
    [encoder() setBytes:&f length:sizeof(f) atIndex:5];
    [encoder() setBytes:&info length:sizeof(info) atIndex:6];
    dispatch(half ? "rt_composite_f16" : "rt_composite_f32", f.width, f.height);
  }
};

}  // namespace

struct RetroMetalEngine::Impl {
  std::string error;
  std::unique_ptr<Backend> backend;
  std::unique_ptr<gpu::Pipeline<Backend>> pipeline;
};

RetroMetalEngine::RetroMetalEngine(id<MTLDevice> device, const std::string& path) : impl_(new Impl) {
  NSError* err = nil;
  id<MTLLibrary> lib = [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]
                                           error:&err];
  if (!lib) {
    impl_->error = "Cannot load " + path;
    return;
  }
  impl_->backend = std::make_unique<Backend>(device, lib, std::vector<std::string>{
      "rt_prepare_f32", "rt_prepare_f16", "rt_composite_f32", "rt_composite_f16"});
  if (!impl_->backend->ok()) {
    impl_->error = impl_->backend->error();
    return;
  }
  impl_->pipeline = std::make_unique<gpu::Pipeline<Backend>>(*impl_->backend);
}
RetroMetalEngine::~RetroMetalEngine() = default;
bool RetroMetalEngine::ok() const { return impl_ && impl_->error.empty() && impl_->pipeline; }
const std::string& RetroMetalEngine::error() const { return impl_->error; }

bool RetroMetalEngine::encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t w,
                              uint32_t h, uint32_t rowPixels, const Params& params, int64_t frameKey, bool rgba,
                              bool bottomUp) {
  if (!ok()) return false;
  Backend& B = *impl_->backend;
  B.src = (__bridge const void*)src;
  B.dst = (__bridge void*)dst;
  B.begin(cb);
  const bool done = impl_->pipeline->run(B.src, B.dst, half, w, h, rowPixels, params, frameKey, rgba, bottomUp);
  B.end();
  return done;
}

}  // namespace rt
