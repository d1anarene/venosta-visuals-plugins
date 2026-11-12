// Venosta Visuals engine — Metal backend for vv::gpu::Pyramid and plugin pipelines (Objective-C++,
// ARC). Wraps one compute command encoder: begin(commandBuffer) ... end(). The default compute
// encoder dispatches serially, so every pass sees the complete output of the previous one.
// Plugins derive and add their own kernels (loaded by name from the same metallib).
#pragma once
#import <Metal/Metal.h>

#include <map>
#include <string>
#include <vector>

#include "gpu/vv_gpu_pixels.h"

namespace vv {
namespace gpu {

class MetalBackend {
 public:
  using Buf = id<MTLBuffer>;

  /** Loads the generic kernels (vv_common.metal) plus `kernels` from `library`. */
  MetalBackend(id<MTLDevice> device, id<MTLLibrary> library, const std::vector<std::string>& kernels);
  bool ok() const { return error_.empty(); }
  const std::string& error() const { return error_; }
  id<MTLDevice> device() const { return device_; }

  void begin(id<MTLCommandBuffer> cb) { enc_ = [cb computeCommandEncoder]; }
  void end() {
    [enc_ endEncoding];
    enc_ = nil;
  }
  id<MTLComputeCommandEncoder> encoder() const { return enc_; }

  Buf alloc(size_t elements);  // F4 elements, GPU-private storage
  void releaseAll() { owned_.clear(); }

  /** Run kernel `name` over a w x h grid with the buffers/bytes already set on encoder(). */
  void dispatch(const char* name, uint32_t w, uint32_t h);

  // Pyramid operations (see gpu/vv_gpu_pyramid.h).
  void downsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) { resample("vv_downsample", s, d, si, di); }
  void downsampleX(Buf s, Buf d, PlaneInfo si, PlaneInfo di) { resample("vv_downsample_x", s, d, si, di); }
  void upsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) { resample("vv_upsample", s, d, si, di); }
  void upsampleX(Buf s, Buf d, PlaneInfo si, PlaneInfo di) { resample("vv_upsample_x", s, d, si, di); }
  void blurH(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) { blur("vv_blur_h", s, d, p, k); }
  void blurV(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) { blur("vv_blur_v", s, d, p, k); }
  void axpy(Buf acc, Buf x, float w, PlaneInfo p);
  void clear(Buf d, PlaneInfo p);

 private:
  void resample(const char* name, Buf s, Buf d, PlaneInfo si, PlaneInfo di);
  void blur(const char* name, Buf s, Buf d, PlaneInfo p, const std::vector<float>& k);

  id<MTLDevice> device_ = nil;
  id<MTLComputeCommandEncoder> enc_ = nil;
  std::map<std::string, id<MTLComputePipelineState>> pso_;
  std::vector<Buf> owned_;
  std::string error_;
};

}  // namespace gpu
}  // namespace vv
