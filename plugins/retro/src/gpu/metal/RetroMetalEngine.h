// Retro — Metal engine (macOS) on the shared MetalBackend.
#pragma once
#import <Metal/Metal.h>

#include <memory>
#include <string>

#include "rt.h"

namespace rt {
class RetroMetalEngine {
 public:
  RetroMetalEngine(id<MTLDevice> device, const std::string& metallibPath);
  ~RetroMetalEngine();
  bool ok() const;
  const std::string& error() const;
  /** Encode one frame into `cb` (caller commits / waits). */
  bool encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t width,
              uint32_t height, uint32_t rowPixels, const Params& params, int64_t frameKey, bool rgba, bool bottomUp);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace rt
