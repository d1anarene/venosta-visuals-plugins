// Denoise — Metal engine (macOS): spatial filter on the shared MetalBackend.
#pragma once
#import <Metal/Metal.h>

#include <memory>
#include <string>

#include "dn_profile.h"
#include "dn_spatial.h"

namespace dn {
class DnMetalEngine {
 public:
  DnMetalEngine(id<MTLDevice> device, const std::string& metallibPath);
  ~DnMetalEngine();
  bool ok() const;
  const std::string& error() const;
  /** Encode one frame into `cb` (caller commits / waits). */
  bool encode(id<MTLCommandBuffer> cb, id<MTLBuffer> src, id<MTLBuffer> dst, bool half, uint32_t width, uint32_t height,
              uint32_t rowPixels, const NoiseProfile& profile, const SpatialParams& params, bool rgba,
              int view = 0, float mix = 1.0f);
  /** Centred crop (at most maxW x maxH) as RGBA float, top row first, for noise estimation. Uses
   *  its own command buffer on `queue` (call before encoding the frame). */
  bool downloadCrop(id<MTLCommandQueue> queue, id<MTLBuffer> src, bool half, uint32_t width, uint32_t height,
                    uint32_t rowPixels, bool rgba, bool bottomUp, uint32_t maxW, uint32_t maxH, vv::Image& out);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace dn
