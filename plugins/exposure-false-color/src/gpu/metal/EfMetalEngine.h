// Exposure & False Color — Metal engine (macOS) on the shared MetalBackend.
#pragma once
#import <Metal/Metal.h>

#include <memory>
#include <string>

#include "ef.h"

namespace ef {
class EfMetalEngine {
 public:
  EfMetalEngine(id<MTLDevice> device, const std::string& metallibPath);
  ~EfMetalEngine();
  bool ok() const;
  const std::string& error() const;
  /** Encode one frame into `cb` (caller commits / waits). */
  bool encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t width,
              uint32_t height, uint32_t rowPixels, const Params& params, bool rgba, bool bottomUp);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace ef
