// Denoise — Metal engine (macOS): spatial filter on the shared MetalBackend.
#pragma once
#import <Metal/Metal.h>

#include <memory>
#include <string>
#include <vector>

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
              int view = 0, float mix = 1.0f, bool preview = false);
  /** Device part of the noise estimate on a centred crop (at most maxW x maxH): block statistics
   *  of every starlet band, for estimateFromBlocks. Own command buffer on `queue`; waits. */
  bool estimateBlocks(id<MTLCommandQueue> queue, id<MTLBuffer> src, bool half, uint32_t width, uint32_t height,
                      uint32_t rowPixels, bool rgba, bool bottomUp, uint32_t maxW, uint32_t maxH,
                      std::vector<std::vector<EstimateBlock>>& out, int& cropW, int& cropH, bool preview = false);
  /** Rows y0..y0+h-1 (as stored) of a frame buffer <-> RGBA float image (diagnostic overlays).
   *  Each uses its own command buffer on `queue` and waits. */
  bool readRegion(id<MTLCommandQueue> queue, id<MTLBuffer> buf, bool half, uint32_t rowPixels, bool rgba, uint32_t x0,
                  uint32_t y0, uint32_t w, uint32_t h, vv::Image& out);
  bool writeRegion(id<MTLCommandQueue> queue, id<MTLBuffer> buf, bool half, uint32_t rowPixels, bool rgba, uint32_t x0,
                   uint32_t y0, const vv::Image& in);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace dn
