// CUDA renderer (default stream, finished on return). STATUS: compiled in CI; not run in a host.
#include "RtGpu.h"
#include "RtParamsTable.h"

#ifdef RT_WITH_CUDA_RENDERER
#include "RetroCuda.h"
#endif

namespace rt {

#ifdef RT_WITH_CUDA_RENDERER
namespace {
class CudaRenderer : public vv::GpuRenderer {
 public:
  bool prepare(void*) override {
    if (!engine_) engine_ = std::make_unique<RetroCudaEngine>();  // on the host's current CUDA context
    return engine_->ok();
  }
  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t frameKey) override {
    if (!prepare(nullptr)) return false;
    return engine_->render(f.src, f.dst, f.half, f.width, f.height, f.rowPixels, readParams(r), frameKey, f.rgba,
                           f.bottomUp);
  }

 private:
  std::unique_ptr<RetroCudaEngine> engine_;
};
}  // namespace
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return std::make_unique<CudaRenderer>(); }
#else
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return nullptr; }
#endif

#if !defined(__APPLE__) || defined(RT_NO_GPU_RENDERERS)
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return nullptr; }
#endif

}  // namespace rt
