// Film Effects OpenFX plug-in — GPU render backends (CUDA on Windows/Linux, Metal on macOS).
// The host hands over GPU buffers in OpenFX layout (RGBA float, bottom row first); the engines
// are the same ones the Premiere plug-ins use, run with rgba/bottomUp set.
#pragma once
#include <cstdint>
#include <memory>

#include "params/params.h"

namespace fe {
namespace ofx {

class GpuRenderer {
 public:
  virtual ~GpuRenderer() = default;
  /** `queue` is the host's Metal command queue (Metal) or CUDA stream (unused: default stream).
   *  `src`/`dst` are the host's GPU buffers (MTLBuffer / device pointer), RGBA 32-bit float.
   *  Returns false on failure; the frame must then be reported as failed. */
  virtual bool render(void* queue, const void* src, void* dst, uint32_t width, uint32_t height, uint32_t rowPixels,
                      const FilmParams& params, int64_t frameKey) = 0;
};

/** nullptr when this build has no such backend. */
std::unique_ptr<GpuRenderer> makeCudaRenderer();
std::unique_ptr<GpuRenderer> makeMetalRenderer();

}  // namespace ofx
}  // namespace fe
