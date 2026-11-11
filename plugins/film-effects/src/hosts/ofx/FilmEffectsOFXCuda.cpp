// CUDA backend for the OpenFX plug-in (DaVinci Resolve, NVIDIA). Uses the same engine as the
// Premiere CUDA entry point. The plug-in declares CUDA render support without CUDA streams, so
// the host expects the work to run on the default stream and be finished when render returns
// (CudaFilmEngine::render synchronises before returning).
// STATUS: compiled in CI; not yet run inside DaVinci Resolve.
#include "FilmEffectsOFXGpu.h"

#ifdef FE_OFX_CUDA
#include "CudaFilmEngine.h"
#endif

namespace fe {
namespace ofx {

#ifdef FE_OFX_CUDA
namespace {
class CudaRenderer : public GpuRenderer {
 public:
  bool render(void*, const void* src, void* dst, uint32_t w, uint32_t h, uint32_t rowPixels, const FilmParams& p,
              int64_t key) override {
    if (!engine_) engine_ = std::make_unique<CudaFilmEngine>();  // on the host's current CUDA context
    if (!engine_->ok()) return false;
    return engine_->render(src, dst, false, w, h, rowPixels, p, key, /*rgba=*/true, /*bottomUp=*/true);
  }

 private:
  std::unique_ptr<CudaFilmEngine> engine_;
};
}  // namespace
std::unique_ptr<GpuRenderer> makeCudaRenderer() { return std::make_unique<CudaRenderer>(); }
#else
std::unique_ptr<GpuRenderer> makeCudaRenderer() { return nullptr; }
#endif

#ifndef __APPLE__
std::unique_ptr<GpuRenderer> makeMetalRenderer() { return nullptr; }
#endif

}  // namespace ofx
}  // namespace fe
