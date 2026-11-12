#include <cuda_runtime.h>

#include "RetroCuda.h"
#include "gpu/cuda/vv_cuda_backend.cuh"
#include "rt_gpu_pipeline.h"

namespace rt {
namespace {
using namespace rt::gpu;
using vv::gpu::cuda::coords;

template <typename T>
__global__ void kPrepare(const T* src, F4* yuv, FrameLayout f) {
  uint32_t x, y;
  if (coords(f.width, f.height, x, y)) pxRtPrepare(src, yuv, f, x, y);
}
template <typename T>
__global__ void kComposite(const T* src, T* dst, const F4* l, const F4* r, const F4* c, FrameLayout f, RtInfo info) {
  uint32_t x, y;
  if (coords(f.width, f.height, x, y)) pxRtComposite(src, dst, l, r, c, f, info, x, y);
}

struct Backend : vv::gpu::cuda::CudaBackendBase {
  void rtPrepare(const void* src, bool half, Buf yuv, const FrameLayout& f) {
    if (half) kPrepare<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const H4*>(src), yuv, f);
    else kPrepare<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const F4*>(src), yuv, f);
    check();
  }
  void rtComposite(const void* src, void* dst, bool half, Buf l, Buf r, Buf c, const FrameLayout& f, const RtInfo& info) {
    if (half)
      kComposite<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const H4*>(src), static_cast<H4*>(dst), l, r, c, f, info);
    else
      kComposite<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const F4*>(src), static_cast<F4*>(dst), l, r, c, f, info);
    check();
  }
};
}  // namespace

struct RetroCudaEngine::Impl {
  Backend backend;
  std::unique_ptr<gpu::Pipeline<Backend>> pipeline;
  bool ok = false;
};

RetroCudaEngine::RetroCudaEngine() : impl_(new Impl) {
  int device = -1;
  impl_->ok = cudaGetDevice(&device) == cudaSuccess && device >= 0;
  impl_->pipeline.reset(new gpu::Pipeline<Backend>(impl_->backend));
}
RetroCudaEngine::~RetroCudaEngine() {
  if (!impl_) return;
  impl_->pipeline.reset();
  impl_->backend.releaseAll();
}
bool RetroCudaEngine::ok() const { return impl_ && impl_->ok; }

bool RetroCudaEngine::render(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels,
                             const Params& params, int64_t frameKey, bool rgba, bool bottomUp) {
  if (!ok()) return false;
  Backend& B = impl_->backend;
  B.err = cudaSuccess;
  if (!sanitized(params).anyActive()) {
    if (src != dst) B.note(cudaMemcpyAsync(dst, src, size_t(rowPixels) * h * (half ? 8 : 16), cudaMemcpyDeviceToDevice, B.stream));
  } else if (!impl_->pipeline->run(src, dst, half, w, h, rowPixels, params, frameKey, rgba, bottomUp)) {
    return false;
  }
  B.note(cudaStreamSynchronize(B.stream));
  return B.err == cudaSuccess;
}
}  // namespace rt
