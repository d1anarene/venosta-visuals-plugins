// Lens & Diffusion — CUDA engine (NVIDIA). Host-API independent: device pointers + LensParams.
// STATUS: compiled with nvcc in CI; not yet run on an NVIDIA GPU.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "lens_params.h"

namespace ld {

class LensCudaEngine {
 public:
  LensCudaEngine();
  ~LensCudaEngine();
  LensCudaEngine(const LensCudaEngine&) = delete;
  LensCudaEngine& operator=(const LensCudaEngine&) = delete;
  bool ok() const;
  /** One frame on the current CUDA device (default stream, finished on return). `src`/`dst` may be
   *  the same. half: 16-bit float frame. rgba: RGBA (OpenFX) instead of BGRA (Premiere). */
  bool render(const void* src, void* dst, bool half, uint32_t width, uint32_t height, uint32_t rowPixels,
              const LensParams& params, bool rgba);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ld
