// CUDA renderer: default stream, finished before render() returns (no CUDA stream support declared).
// STATUS: compiled in CI; not yet run on an NVIDIA GPU inside a host.
#include "LensGpu.h"
#include "LensParamsTable.h"

#ifdef LD_WITH_CUDA_RENDERER
#include "LensCudaEngine.h"
#endif

namespace ld {

#ifdef LD_WITH_CUDA_RENDERER
namespace {
class CudaRenderer : public vv::GpuRenderer {
 public:
  bool prepare(void*) override {
    if (!engine_) engine_ = std::make_unique<LensCudaEngine>();  // on the host's current CUDA context
    return engine_->ok();
  }
  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t) override {
    if (!prepare(nullptr)) return false;
    return engine_->render(f.src, f.dst, f.half, f.width, f.height, f.rowPixels, readParams(r), f.rgba);
  }

 private:
  std::unique_ptr<LensCudaEngine> engine_;
};
}  // namespace
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return std::make_unique<CudaRenderer>(); }
#else
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return nullptr; }
#endif

#if !defined(__APPLE__) || defined(LD_NO_GPU_RENDERERS)
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return nullptr; }
#endif

}  // namespace ld
