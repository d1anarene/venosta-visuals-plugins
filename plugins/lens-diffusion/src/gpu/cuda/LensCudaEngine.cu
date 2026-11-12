// Lens & Diffusion — CUDA backend: shared engine kernels (vv_cuda_backend.cuh) + this effect's
// passes (src/gpu/common/ld_gpu_pixels.h), orchestrated by the shared pass sequence
// (src/gpu/common/ld_gpu_pipeline.h) that the CPU emulation test compares with the reference.
#include <cuda_runtime.h>

#include "LensCudaEngine.h"
#include "gpu/cuda/vv_cuda_backend.cuh"
#include "ld_gpu_pipeline.h"

namespace ld {
namespace {

using namespace ld::gpu;
using vv::gpu::cuda::coords;

template <typename T>
__global__ void kPrepare(const T* src, F4* lin, FrameInfo fi) {
  uint32_t x, y;
  if (coords(fi.width, fi.height, x, y)) pxLdPrepare(src, lin, fi, x, y);
}
__global__ void kAberrate(const F4* lin, F4* out, AberrationInfo a) {
  uint32_t x, y;
  if (coords(a.width, a.height, x, y)) pxLdAberrate(lin, out, a, x, y);
}
__global__ void kWeights(const F4* lin, F4* s, F4* b, WeightInfo wi) {
  uint32_t x, y;
  if (coords(wi.width, wi.height, x, y)) pxLdWeights(lin, s, b, wi, x, y);
}
template <typename T>
__global__ void kComposite(const T* src, T* dst, const F4* lin, const F4* s, const F4* scat, const F4* streak,
                           CompositeInfo ci) {
  uint32_t x, y;
  if (coords(ci.width, ci.height, x, y)) pxLdComposite(src, dst, lin, s, scat, streak, ci, x, y);
}

struct Backend : vv::gpu::cuda::CudaBackendBase {
  void ldPrepare(const void* src, bool half, Buf lin, const FrameInfo& fi) {
    if (half) kPrepare<<<grid(fi.width, fi.height), block(), 0, stream>>>(static_cast<const H4*>(src), lin, fi);
    else kPrepare<<<grid(fi.width, fi.height), block(), 0, stream>>>(static_cast<const F4*>(src), lin, fi);
    check();
  }
  void ldAberrate(Buf lin, Buf out, const AberrationInfo& a) {
    kAberrate<<<grid(a.width, a.height), block(), 0, stream>>>(lin, out, a);
    check();
  }
  void ldWeights(Buf lin, Buf s, Buf b, const WeightInfo& wi) {
    kWeights<<<grid(wi.width, wi.height), block(), 0, stream>>>(lin, s, b, wi);
    check();
  }
  void ldComposite(const void* src, void* dst, bool half, Buf lin, Buf s, Buf scat, Buf streak, const CompositeInfo& ci) {
    if (half)
      kComposite<<<grid(ci.width, ci.height), block(), 0, stream>>>(static_cast<const H4*>(src), static_cast<H4*>(dst),
                                                                    lin, s, scat, streak, ci);
    else
      kComposite<<<grid(ci.width, ci.height), block(), 0, stream>>>(static_cast<const F4*>(src), static_cast<F4*>(dst),
                                                                    lin, s, scat, streak, ci);
    check();
  }
};

}  // namespace

struct LensCudaEngine::Impl {
  Backend backend;
  std::unique_ptr<gpu::Pipeline<Backend>> pipeline;
  bool ok = false;
};

LensCudaEngine::LensCudaEngine() : impl_(new Impl) {
  int device = -1;
  impl_->ok = cudaGetDevice(&device) == cudaSuccess && device >= 0;  // host's current context
  impl_->pipeline.reset(new gpu::Pipeline<Backend>(impl_->backend));
}
LensCudaEngine::~LensCudaEngine() {
  if (!impl_) return;
  impl_->pipeline.reset();
  impl_->backend.releaseAll();
}
bool LensCudaEngine::ok() const { return impl_ && impl_->ok; }

bool LensCudaEngine::render(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels,
                            const LensParams& params, bool rgba) {
  if (!ok()) return false;
  Backend& B = impl_->backend;
  B.err = cudaSuccess;
  if (!params.anyEffectActive()) {  // bypass: exact copy when not in place
    if (src != dst) B.note(cudaMemcpyAsync(dst, src, size_t(rowPixels) * h * (half ? 8 : 16), cudaMemcpyDeviceToDevice, B.stream));
  } else if (!impl_->pipeline->run(src, dst, half, w, h, rowPixels, params, rgba)) {
    return false;
  }
  B.note(cudaStreamSynchronize(B.stream));
  return B.err == cudaSuccess;
}

}  // namespace ld
