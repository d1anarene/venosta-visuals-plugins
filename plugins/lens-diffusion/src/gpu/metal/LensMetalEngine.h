// Lens & Diffusion — Metal engine (macOS): shared MetalBackend + this effect's kernels, driven by the
// shared pass sequence (src/gpu/common/ld_gpu_pipeline.h).
#pragma once
#import <Metal/Metal.h>

#include <memory>
#include <string>

#include "lens_params.h"

namespace ld {

class LensMetalEngine {
 public:
  LensMetalEngine(id<MTLDevice> device, const std::string& metallibPath);
  ~LensMetalEngine();
  bool ok() const;
  const std::string& error() const;
  /** Encode one frame into `cb` (caller commits / waits). src/dst: float4 or half4 buffers. */
  bool encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t width, uint32_t height,
              uint32_t rowPixels, const LensParams& params, bool rgba);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ld
