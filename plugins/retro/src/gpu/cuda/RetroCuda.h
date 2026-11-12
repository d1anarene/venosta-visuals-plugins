// Retro — CUDA engine (NVIDIA). Default stream, finished on return.
#pragma once
#include <cstdint>
#include <memory>

#include "rt.h"

namespace rt {
class RetroCudaEngine {
 public:
  RetroCudaEngine();
  ~RetroCudaEngine();
  bool ok() const;
  bool render(const void* src, void* dst, bool half, uint32_t width, uint32_t height, uint32_t rowPixels,
              const Params& params, int64_t frameKey, bool rgba, bool bottomUp);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace rt
