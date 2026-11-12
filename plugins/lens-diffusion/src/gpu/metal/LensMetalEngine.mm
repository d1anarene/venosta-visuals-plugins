#import "LensMetalEngine.h"

#include "gpu/metal/vv_metal_backend.h"
#include "ld_gpu_pipeline.h"

namespace ld {
namespace {

using namespace ld::gpu;

struct Backend : vv::gpu::MetalBackend {
  using vv::gpu::MetalBackend::MetalBackend;
  const void* src = nullptr;  // current frame (id<MTLBuffer> bridged)
  void* dst = nullptr;

  void ldPrepare(const void*, bool half, Buf lin, const FrameInfo& fi) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)src offset:0 atIndex:0];
    [encoder() setBuffer:lin offset:0 atIndex:1];
    [encoder() setBytes:&fi length:sizeof(fi) atIndex:2];
    dispatch(half ? "ld_prepare_f16" : "ld_prepare_f32", fi.width, fi.height);
  }
  void ldAberrate(Buf lin, Buf out, const AberrationInfo& a) {
    [encoder() setBuffer:lin offset:0 atIndex:0];
    [encoder() setBuffer:out offset:0 atIndex:1];
    [encoder() setBytes:&a length:sizeof(a) atIndex:2];
    dispatch("ld_aberrate", a.width, a.height);
  }
  void ldWeights(Buf lin, Buf s, Buf b, const WeightInfo& wi) {
    [encoder() setBuffer:lin offset:0 atIndex:0];
    [encoder() setBuffer:s offset:0 atIndex:1];
    [encoder() setBuffer:b offset:0 atIndex:2];
    [encoder() setBytes:&wi length:sizeof(wi) atIndex:3];
    dispatch("ld_weights", wi.width, wi.height);
  }
  void ldComposite(const void*, void*, bool half, Buf lin, Buf s, Buf scat, Buf streak, const CompositeInfo& ci) {
    [encoder() setBuffer:(__bridge id<MTLBuffer>)src offset:0 atIndex:0];
    [encoder() setBuffer:(__bridge id<MTLBuffer>)dst offset:0 atIndex:1];
    [encoder() setBuffer:lin offset:0 atIndex:2];
    [encoder() setBuffer:s offset:0 atIndex:3];
    [encoder() setBuffer:scat offset:0 atIndex:4];
    [encoder() setBuffer:streak offset:0 atIndex:5];
    [encoder() setBytes:&ci length:sizeof(ci) atIndex:6];
    dispatch(half ? "ld_composite_f16" : "ld_composite_f32", ci.width, ci.height);
  }
};

id<MTLLibrary> loadLibrary(id<MTLDevice> device, const std::string& path, std::string& error) {
  NSError* err = nil;
  id<MTLLibrary> lib = [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]
                                           error:&err];
  if (!lib) error = "Cannot load " + path;
  return lib;
}

}  // namespace

struct LensMetalEngine::Impl {
  std::string error;
  std::unique_ptr<Backend> backend;
  std::unique_ptr<gpu::Pipeline<Backend>> pipeline;
};

LensMetalEngine::LensMetalEngine(id<MTLDevice> device, const std::string& metallibPath) : impl_(new Impl) {
  id<MTLLibrary> lib = loadLibrary(device, metallibPath, impl_->error);
  if (!lib) return;
  impl_->backend = std::make_unique<Backend>(device, lib, std::vector<std::string>{
      "ld_prepare_f32", "ld_prepare_f16", "ld_aberrate", "ld_weights", "ld_composite_f32", "ld_composite_f16"});
  if (!impl_->backend->ok()) {
    impl_->error = impl_->backend->error();
    return;
  }
  impl_->pipeline = std::make_unique<gpu::Pipeline<Backend>>(*impl_->backend);
}
LensMetalEngine::~LensMetalEngine() = default;
bool LensMetalEngine::ok() const { return impl_ && impl_->error.empty() && impl_->pipeline; }
const std::string& LensMetalEngine::error() const { return impl_->error; }

bool LensMetalEngine::encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t w,
                             uint32_t h, uint32_t rowPixels, const LensParams& params, bool rgba) {
  if (!ok()) return false;
  Backend& B = *impl_->backend;
  B.src = (__bridge const void*)src;
  B.dst = (__bridge void*)dst;
  B.begin(cb);
  const bool done = impl_->pipeline->run(B.src, B.dst, half, w, h, rowPixels, params, rgba);
  B.end();
  return done;
}

}  // namespace ld
