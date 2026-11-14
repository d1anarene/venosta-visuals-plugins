// Denoise — CUDA engine (NVIDIA): spatial filter. Default stream, finished on return.
#pragma once
#include <cstdint>
#include <memory>

#include "dn_profile.h"
#include "dn_spatial.h"

namespace dn {
class DnCudaEngine {
 public:
  DnCudaEngine();
  ~DnCudaEngine();
  bool ok() const;
  bool render(const void* src, void* dst, bool half, uint32_t width, uint32_t height, uint32_t rowPixels,
              const NoiseProfile& profile, const SpatialParams& params, bool rgba, int view, float mix);
  /** Copies a centred crop (at most maxW x maxH) of the frame to the host as RGBA float, top row
   *  first, for noise estimation. */
  bool downloadCrop(const void* src, bool half, uint32_t width, uint32_t height, uint32_t rowPixels, bool rgba,
                    bool bottomUp, uint32_t maxW, uint32_t maxH, vv::Image& out);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace dn
