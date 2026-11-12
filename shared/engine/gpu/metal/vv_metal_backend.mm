#import "vv_metal_backend.h"

#include <algorithm>

namespace vv {
namespace gpu {

namespace {
struct BlurInfoHost {
  uint32_t width, height;
  int32_t radius;
};
}  // namespace

MetalBackend::MetalBackend(id<MTLDevice> device, id<MTLLibrary> library, const std::vector<std::string>& kernels)
    : device_(device) {
  if (!device || !library) {
    error_ = "no Metal device or library";
    return;
  }
  std::vector<std::string> names = {"vv_downsample", "vv_downsample_x", "vv_upsample", "vv_upsample_x",
                                    "vv_blur_h",     "vv_blur_v",       "vv_axpy",     "vv_clear"};
  names.insert(names.end(), kernels.begin(), kernels.end());
  for (const std::string& n : names) {
    id<MTLFunction> fn = [library newFunctionWithName:[NSString stringWithUTF8String:n.c_str()]];
    NSError* err = nil;
    id<MTLComputePipelineState> ps = fn ? [device newComputePipelineStateWithFunction:fn error:&err] : nil;
    if (!ps) {
      error_ = "Missing Metal kernel " + n;
      return;
    }
    pso_[n] = ps;
  }
}

MetalBackend::Buf MetalBackend::alloc(size_t elements) {
  Buf b = [device_ newBufferWithLength:std::max<size_t>(elements * sizeof(float) * 4, 16)
                               options:MTLResourceStorageModePrivate];
  owned_.push_back(b);
  return b;
}

void MetalBackend::dispatch(const char* name, uint32_t w, uint32_t h) {
  id<MTLComputePipelineState> ps = pso_[name];
  [enc_ setComputePipelineState:ps];
  NSUInteger tw = ps.threadExecutionWidth;
  NSUInteger th = std::max<NSUInteger>(1, ps.maxTotalThreadsPerThreadgroup / tw);
  [enc_ dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
}

void MetalBackend::resample(const char* name, Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
  [enc_ setBuffer:s offset:0 atIndex:0];
  [enc_ setBuffer:d offset:0 atIndex:1];
  [enc_ setBytes:&si length:sizeof(si) atIndex:2];
  [enc_ setBytes:&di length:sizeof(di) atIndex:3];
  dispatch(name, di.width, di.height);
}

void MetalBackend::blur(const char* name, Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) {
  // The pyramid plans keep per-level sigmas small; setBytes allows up to 4 KB (1024 taps).
  BlurInfoHost bi{p.width, p.height, int32_t(k.size() / 2)};
  const size_t bytes = std::min<size_t>(k.size() * sizeof(float), 4096);
  [enc_ setBuffer:s offset:0 atIndex:0];
  [enc_ setBuffer:d offset:0 atIndex:1];
  [enc_ setBytes:&bi length:sizeof(bi) atIndex:2];
  [enc_ setBytes:k.data() length:bytes atIndex:3];
  dispatch(name, p.width, p.height);
}

void MetalBackend::axpy(Buf acc, Buf x, float w, PlaneInfo p) {
  [enc_ setBuffer:acc offset:0 atIndex:0];
  [enc_ setBuffer:x offset:0 atIndex:1];
  [enc_ setBytes:&w length:sizeof(w) atIndex:2];
  [enc_ setBytes:&p length:sizeof(p) atIndex:3];
  dispatch("vv_axpy", p.width, p.height);
}

void MetalBackend::clear(Buf d, PlaneInfo p) {
  [enc_ setBuffer:d offset:0 atIndex:0];
  [enc_ setBytes:&p length:sizeof(p) atIndex:1];
  dispatch("vv_clear", p.width, p.height);
}

}  // namespace gpu
}  // namespace vv
