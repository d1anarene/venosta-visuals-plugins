// Film Effects — CUDA backend (Windows/NVIDIA). Host-API independent: takes device pointers and
// core FilmParams, so it can be driven by Premiere's GPU entry point or a standalone test.
// The per-pixel maths and pass order are shared with the CPU emulation test
// (tests/gpu/gpu_emulation_test.cpp), which proves them equal to the CPU reference.
// STATUS: compiled with nvcc in CI; not yet run on an NVIDIA GPU.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "params/params.h"

namespace fe {

class CudaFilmEngine {
 public:
  CudaFilmEngine();
  ~CudaFilmEngine();
  CudaFilmEngine(const CudaFilmEngine&) = delete;
  CudaFilmEngine& operator=(const CudaFilmEngine&) = delete;

  bool ok() const;
  const std::string& error() const;

  /** Process one frame on the current CUDA device. `src`/`dst` are device pointers (may be
   *  the same, in place); `half` = 16-bit float frame (GPU_BGRA_4444_16f), else 32-bit float.
   *  Default layout: BGRA, top row first (Premiere). OpenFX hosts: rgba = true, bottomUp = true.
   *  Work runs on the default stream and is finished before this returns. */
  bool render(const void* src, void* dst, bool half, uint32_t width, uint32_t height, uint32_t rowPixels,
              const FilmParams& params, int64_t frameKey, bool rgba = false, bool bottomUp = false);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fe
