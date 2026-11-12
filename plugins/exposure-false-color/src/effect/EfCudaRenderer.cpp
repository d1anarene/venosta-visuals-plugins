// CUDA renderer (default stream, finished on return). STATUS: compiled in CI; not run in a host.
#include "EfGpu.h"
#include "EfParamsTable.h"

#ifdef EF_WITH_CUDA_RENDERER
#include <cuda_runtime.h>

#include "EfCuda.h"
#endif

namespace ef {

#ifdef EF_WITH_CUDA_RENDERER
namespace {
class CudaRenderer : public vv::GpuRenderer {
 public:
  bool prepare(void*) override {
    int device = -1;
    return cudaGetDevice(&device) == cudaSuccess && device >= 0;  // the host's current CUDA context
  }
  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t) override {
    return cudaRender(f.src, f.dst, f.half, f.width, f.height, f.rowPixels, readParams(r), f.rgba, f.bottomUp);
  }
};
}  // namespace
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return std::make_unique<CudaRenderer>(); }
#else
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return nullptr; }
#endif

#if !defined(__APPLE__) || defined(EF_NO_GPU_RENDERERS)
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return nullptr; }
#endif

}  // namespace ef
