#import "EfMetalEngine.h"

#include "gpu/metal/vv_metal_backend.h"

namespace ef {

struct EfMetalEngine::Impl {
  std::string error;
  std::unique_ptr<vv::gpu::MetalBackend> backend;
};

namespace {
struct FrameLayoutHost {
  uint32_t rowPixels;
  int32_t rgba, bottomUp;
};
}  // namespace

EfMetalEngine::EfMetalEngine(id<MTLDevice> device, const std::string& path) : impl_(new Impl) {
  NSError* err = nil;
  id<MTLLibrary> lib = [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]
                                           error:&err];
  if (!lib) {
    impl_->error = "Cannot load " + path;
    return;
  }
  impl_->backend = std::make_unique<vv::gpu::MetalBackend>(device, lib, std::vector<std::string>{"ef_f32", "ef_f16"});
  if (!impl_->backend->ok()) impl_->error = impl_->backend->error();
}
EfMetalEngine::~EfMetalEngine() = default;
bool EfMetalEngine::ok() const { return impl_ && impl_->error.empty(); }
const std::string& EfMetalEngine::error() const { return impl_->error; }

bool EfMetalEngine::encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t w,
                           uint32_t h, uint32_t rowPixels, const Params& raw, bool rgba, bool bottomUp) {
  if (!ok()) return false;
  const Params p = sanitized(raw);
  const EfInfo info = ef::info(p, int(w), int(h));
  const FrameLayoutHost f{rowPixels, rgba ? 1 : 0, bottomUp ? 1 : 0};
  vv::gpu::MetalBackend& B = *impl_->backend;
  B.begin(cb);
  [B.encoder() setBuffer:src offset:0 atIndex:0];
  [B.encoder() setBuffer:dst offset:0 atIndex:1];
  [B.encoder() setBytes:&info length:sizeof(info) atIndex:2];
  [B.encoder() setBytes:&f length:sizeof(f) atIndex:3];
  B.dispatch(half ? "ef_f16" : "ef_f32", w, h);
  B.end();
  return true;
}

}  // namespace ef
