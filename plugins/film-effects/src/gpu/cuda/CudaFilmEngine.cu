// Film Effects — CUDA backend. Kernels are thin wrappers around src/gpu/common/fe_gpu_pixels.h;
// orchestration is the shared src/gpu/common/fe_gpu_pipeline.h.
// Build: nvcc -std=c++17 --fmad=false (keeps results closest to the CPU reference).
#include <cuda_runtime.h>

#include <cstring>
#include <vector>

#include "CudaFilmEngine.h"
#include "gpu/common/fe_gpu_pipeline.h"

namespace fe {
namespace {

using gpu::CompositeInfo;
using gpu::F4;
using gpu::FrameInfo;
using gpu::H4;
using gpu::PlaneInfo;

constexpr int kMaxInlineTaps = 1000;  // kernel parameters stay under the 4 KB limit
struct Taps {
  int radius;
  float w[kMaxInlineTaps];
};

__device__ inline bool coords(uint32_t w, uint32_t h, uint32_t& x, uint32_t& y) {
  x = blockIdx.x * blockDim.x + threadIdx.x;
  y = blockIdx.y * blockDim.y + threadIdx.y;
  return x < w && y < h;
}

template <typename T>
__global__ void kPrepare(const T* frame, F4* lin, F4* bright, F4* halSrc, FrameInfo fi) {
  uint32_t x, y;
  if (coords(fi.width, fi.height, x, y)) gpu::pxPrepare(frame, lin, bright, halSrc, fi, x, y);
}
__global__ void kDownsample(const F4* s, F4* d, PlaneInfo si, PlaneInfo di) {
  uint32_t x, y;
  if (coords(di.width, di.height, x, y)) gpu::pxDownsample(s, d, si, di, x, y);
}
__global__ void kBlurH(const F4* s, F4* d, PlaneInfo p, Taps t) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) gpu::pxBlurH(s, d, p, t.w, t.radius, x, y);
}
__global__ void kBlurV(const F4* s, F4* d, PlaneInfo p, Taps t) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) gpu::pxBlurV(s, d, p, t.w, t.radius, x, y);
}
__global__ void kBlurHBig(const F4* s, F4* d, PlaneInfo p, const float* w, int radius) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) gpu::pxBlurH(s, d, p, w, radius, x, y);
}
__global__ void kBlurVBig(const F4* s, F4* d, PlaneInfo p, const float* w, int radius) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) gpu::pxBlurV(s, d, p, w, radius, x, y);
}
__global__ void kUpsample(const F4* s, F4* d, PlaneInfo si, PlaneInfo di) {
  uint32_t x, y;
  if (coords(di.width, di.height, x, y)) gpu::pxUpsample(s, d, si, di, x, y);
}
__global__ void kAxpy(F4* acc, const F4* v, float w, PlaneInfo p) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) gpu::pxAxpy(acc, v, w, p, x, y);
}
__global__ void kClear(F4* d, PlaneInfo p) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) gpu::pxClear(d, p, x, y);
}
template <typename T>
__global__ void kComposite(const T* src, T* dst, const F4* lin, const F4* bloom, const F4* hn, const F4* hf,
                           const F4* hs, CompositeInfo ci) {
  uint32_t x, y;
  if (coords(ci.width, ci.height, x, y)) gpu::pxComposite(src, dst, lin, bloom, hn, hf, hs, ci, x, y);
}

struct CudaBackend {
  using Buf = F4*;
  cudaStream_t stream = nullptr;
  std::vector<void*> owned;
  cudaError_t err = cudaSuccess;

  ~CudaBackend() { releaseAll(); }
  void releaseAll() {
    for (void* p : owned) cudaFree(p);
    owned.clear();
  }
  void note(cudaError_t e) {
    if (e != cudaSuccess && err == cudaSuccess) err = e;
  }
  Buf alloc(size_t n) {
    void* p = nullptr;
    note(cudaMalloc(&p, n * sizeof(F4)));
    if (p) owned.push_back(p);
    return static_cast<Buf>(p);
  }
  static dim3 grid(uint32_t w, uint32_t h) { return dim3((w + 15) / 16, (h + 15) / 16); }
  static dim3 block() { return dim3(16, 16); }
  void check() { note(cudaGetLastError()); }

  void prepare(const void* frame, bool half, Buf lin, Buf bright, Buf halSrc, const FrameInfo& fi) {
    if (half) kPrepare<<<grid(fi.width, fi.height), block(), 0, stream>>>(static_cast<const H4*>(frame), lin, bright, halSrc, fi);
    else kPrepare<<<grid(fi.width, fi.height), block(), 0, stream>>>(static_cast<const F4*>(frame), lin, bright, halSrc, fi);
    check();
  }
  void downsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    kDownsample<<<grid(di.width, di.height), block(), 0, stream>>>(s, d, si, di);
    check();
  }
  void blurPass(bool horizontal, Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) {
    const int radius = int(k.size() / 2);
    if (int(k.size()) <= kMaxInlineTaps) {
      Taps t;
      t.radius = radius;
      std::memcpy(t.w, k.data(), k.size() * sizeof(float));
      if (horizontal) kBlurH<<<grid(p.width, p.height), block(), 0, stream>>>(s, d, p, t);
      else kBlurV<<<grid(p.width, p.height), block(), 0, stream>>>(s, d, p, t);
    } else {  // only for extreme aspect ratios; slow path with a temporary weights buffer
      float* w = nullptr;
      note(cudaMalloc(&w, k.size() * sizeof(float)));
      note(cudaMemcpyAsync(w, k.data(), k.size() * sizeof(float), cudaMemcpyHostToDevice, stream));
      if (horizontal) kBlurHBig<<<grid(p.width, p.height), block(), 0, stream>>>(s, d, p, w, radius);
      else kBlurVBig<<<grid(p.width, p.height), block(), 0, stream>>>(s, d, p, w, radius);
      note(cudaStreamSynchronize(stream));
      cudaFree(w);
    }
    check();
  }
  void blurH(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) { blurPass(true, s, d, p, k); }
  void blurV(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) { blurPass(false, s, d, p, k); }
  void upsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    kUpsample<<<grid(di.width, di.height), block(), 0, stream>>>(s, d, si, di);
    check();
  }
  void axpy(Buf acc, Buf v, float w, PlaneInfo p) {
    kAxpy<<<grid(p.width, p.height), block(), 0, stream>>>(acc, v, w, p);
    check();
  }
  void clear(Buf d, PlaneInfo p) {
    kClear<<<grid(p.width, p.height), block(), 0, stream>>>(d, p);
    check();
  }
  void composite(const void* src, void* dst, bool half, Buf lin, Buf bloom, Buf hn, Buf hf, Buf hs,
                 const CompositeInfo& ci) {
    if (half)
      kComposite<<<grid(ci.width, ci.height), block(), 0, stream>>>(static_cast<const H4*>(src), static_cast<H4*>(dst),
                                                                    lin, bloom, hn, hf, hs, ci);
    else
      kComposite<<<grid(ci.width, ci.height), block(), 0, stream>>>(static_cast<const F4*>(src), static_cast<F4*>(dst),
                                                                    lin, bloom, hn, hf, hs, ci);
    check();
  }
};

}  // namespace

struct CudaFilmEngine::Impl {
  CudaBackend backend;
  std::unique_ptr<gpu::Pipeline<CudaBackend>> pipeline;
  std::string error;
};

CudaFilmEngine::CudaFilmEngine() : impl_(new Impl) {
  cudaError_t e = cudaStreamCreateWithFlags(&impl_->backend.stream, cudaStreamNonBlocking);
  if (e != cudaSuccess) {
    impl_->error = std::string("cudaStreamCreate failed: ") + cudaGetErrorString(e);
    return;
  }
  impl_->pipeline.reset(new gpu::Pipeline<CudaBackend>(impl_->backend));
}

CudaFilmEngine::~CudaFilmEngine() {
  if (!impl_) return;
  impl_->pipeline.reset();
  impl_->backend.releaseAll();
  if (impl_->backend.stream) cudaStreamDestroy(impl_->backend.stream);
}

bool CudaFilmEngine::ok() const { return impl_ && impl_->error.empty() && impl_->pipeline; }
const std::string& CudaFilmEngine::error() const { return impl_->error; }

bool CudaFilmEngine::render(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels,
                            const FilmParams& params, int64_t frameKey) {
  if (!ok()) return false;
  CudaBackend& B = impl_->backend;
  B.err = cudaSuccess;
  if (!params.anyEffectActive()) {  // bypass: exact copy when not in place
    if (src != dst) {
      size_t bytes = size_t(rowPixels) * h * (half ? sizeof(H4) : sizeof(F4));
      B.note(cudaMemcpyAsync(dst, src, bytes, cudaMemcpyDeviceToDevice, B.stream));
    }
  } else if (!impl_->pipeline->run(src, dst, half, w, h, rowPixels, params, frameKey)) {
    return false;
  }
  B.note(cudaStreamSynchronize(B.stream));  // the host expects the frame finished when Render returns
  if (B.err != cudaSuccess) {
    impl_->error = cudaGetErrorString(B.err);
    return false;
  }
  return true;
}

}  // namespace fe
