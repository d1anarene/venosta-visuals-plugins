// Denoise — CUDA engine (NVIDIA): spatial filter. Default stream, finished on return.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "dn_profile.h"
#include "dn_spatial.h"

namespace dn {
class DnCudaEngine {
 public:
  DnCudaEngine();
  ~DnCudaEngine();
  bool ok() const;
  bool render(const void* src, void* dst, bool half, uint32_t width, uint32_t height, uint32_t rowPixels,
              const NoiseProfile& profile, const SpatialParams& params, bool rgba, int view, float mix,
              bool preview = false);
  /** Device part of the noise estimate on a centred crop (at most maxW x maxH): block statistics
   *  of every starlet band, for estimateFromBlocks. Waits for the device. */
  bool estimateBlocks(const void* src, bool half, uint32_t width, uint32_t height, uint32_t rowPixels, bool rgba,
                      bool bottomUp, uint32_t maxW, uint32_t maxH, std::vector<std::vector<EstimateBlock>>& out,
                      int& cropW, int& cropH, bool preview = false);
  /** Rows y0..y0+h-1 (as stored) of a frame buffer <-> RGBA float image (diagnostic overlays). */
  bool readRegion(const void* buf, bool half, uint32_t rowPixels, bool rgba, uint32_t x0, uint32_t y0, uint32_t w,
                  uint32_t h, vv::Image& out);
  bool writeRegion(void* buf, bool half, uint32_t rowPixels, bool rgba, uint32_t x0, uint32_t y0, const vv::Image& in);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace dn
