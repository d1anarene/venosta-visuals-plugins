// CUDA renderer (NVIDIA). Work runs on the default stream and is finished before render() returns
// (CudaFilmEngine::render synchronises), which is what Premiere and OpenFX hosts expect when the
// effect does not declare CUDA stream support.
// STATUS: compiled in CI; not yet run on an NVIDIA GPU inside a host.
#include "FilmEffectsGpu.h"
#include "FilmEffectsParams.h"

#ifdef FE_WITH_CUDA_RENDERER
#include "CudaFilmEngine.h"
#endif

namespace fe {

#ifdef FE_WITH_CUDA_RENDERER
namespace {
class CudaRenderer : public vv::GpuRenderer {
 public:
  bool prepare(void*) override {
    if (!engine_) engine_ = std::make_unique<CudaFilmEngine>();  // on the host's current CUDA context
    return engine_->ok();
  }

  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t key) override {
    if (!prepare(nullptr)) return false;
    return engine_->render(f.src, f.dst, f.half, f.width, f.height, f.rowPixels, readParams(r), key, f.rgba,
                           f.bottomUp);
  }

 private:
  std::unique_ptr<CudaFilmEngine> engine_;
};
}  // namespace
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return std::make_unique<CudaRenderer>(); }
#else
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return nullptr; }
#endif

#if !defined(__APPLE__) || defined(FE_NO_GPU_RENDERERS)
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return nullptr; }
#endif

}  // namespace fe
