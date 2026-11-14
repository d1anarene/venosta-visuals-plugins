#include <cuda_runtime.h>

#include <algorithm>
#include <vector>

#include "DnCuda.h"
#include "dn_gpu_pipeline.h"
#include "gpu/cuda/vv_cuda_backend.cuh"

namespace dn {
namespace {
using namespace dn::gpu;
using vv::gpu::cuda::coords;

template <typename T>
__global__ void kPrepare(const T* src, F4* opp, F4* rgb, FrameLayout f) {
  uint32_t x, y;
  if (coords(f.width, f.height, x, y)) pxDnPrepare(src, opp, rgb, f, x, y);
}
__global__ void kStarletH(const F4* s, F4* d, PlaneInfo p, int step) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxDnStarletH(s, d, p, step, x, y);
}
__global__ void kStarletV(const F4* s, F4* d, PlaneInfo p, int step) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxDnStarletV(s, d, p, step, x, y);
}
__global__ void kBand(const F4* c, const F4* n, F4* b, F4* e, PlaneInfo p) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxDnBand(c, n, b, e, p, x, y);
}
__global__ void kVar(const F4* low, const F4* scale, F4* var, DnProfileTable t, BandInfo b) {
  uint32_t x, y;
  if (coords(b.width, b.height, x, y)) pxDnVar(low, scale, var, t, b, x, y);
}
__global__ void kPilot(const F4* band, const F4* E, const F4* var, F4* p2, BandInfo b) {
  uint32_t x, y;
  if (coords(b.width, b.height, x, y)) pxDnPilot(band, E, var, p2, b, x, y);
}
__global__ void kAccumulate(F4* out, const F4* band, const F4* P, const F4* var, BandInfo b) {
  uint32_t x, y;
  if (coords(b.width, b.height, x, y)) pxDnAccumulate(out, band, P, var, b, x, y);
}
template <typename T>
__global__ void kFinish(const T* src, T* dst, const F4* out, const F4* res, FrameLayout f) {
  uint32_t x, y;
  if (coords(f.width, f.height, x, y)) pxDnFinish(src, dst, out, res, f, x, y);
}

struct Backend : vv::gpu::cuda::CudaBackendBase {
  void dnPrepare(const void* src, bool half, Buf opp, Buf rgb, const FrameLayout& f) {
    if (half) kPrepare<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const H4*>(src), opp, rgb, f);
    else kPrepare<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const F4*>(src), opp, rgb, f);
    check();
  }
  void dnStarlet(Buf s, Buf t, Buf d, PlaneInfo p, int step) {
    kStarletH<<<grid(p.width, p.height), block(), 0, stream>>>(s, t, p, step);
    kStarletV<<<grid(p.width, p.height), block(), 0, stream>>>(t, d, p, step);
    check();
  }
  void dnBand(Buf c, Buf n, Buf b, Buf e, PlaneInfo p) {
    kBand<<<grid(p.width, p.height), block(), 0, stream>>>(c, n, b, e, p);
    check();
  }
  void dnVar(Buf low, Buf scale, Buf var, const DnProfileTable& t, const BandInfo& b) {
    kVar<<<grid(b.width, b.height), block(), 0, stream>>>(low, scale, var, t, b);
    check();
  }
  void dnPilot(Buf band, Buf E, Buf var, Buf p2, const BandInfo& b) {
    kPilot<<<grid(b.width, b.height), block(), 0, stream>>>(band, E, var, p2, b);
    check();
  }
  void dnAccumulate(Buf out, Buf band, Buf P, Buf var, const BandInfo& b) {
    kAccumulate<<<grid(b.width, b.height), block(), 0, stream>>>(out, band, P, var, b);
    check();
  }
  void dnFinish(const void* src, void* dst, bool half, Buf out, Buf res, const FrameLayout& f) {
    if (half)
      kFinish<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const H4*>(src), static_cast<H4*>(dst), out, res, f);
    else
      kFinish<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const F4*>(src), static_cast<F4*>(dst), out, res, f);
    check();
  }
};
}  // namespace

struct DnCudaEngine::Impl {
  Backend backend;
  std::unique_ptr<gpu::SpatialPipeline<Backend>> pipeline;
  bool ok = false;
};

DnCudaEngine::DnCudaEngine() : impl_(new Impl) {
  int device = -1;
  impl_->ok = cudaGetDevice(&device) == cudaSuccess && device >= 0;
  impl_->pipeline.reset(new gpu::SpatialPipeline<Backend>(impl_->backend));
}
DnCudaEngine::~DnCudaEngine() {
  if (!impl_) return;
  impl_->pipeline.reset();
  impl_->backend.releaseAll();
}
bool DnCudaEngine::ok() const { return impl_ && impl_->ok; }

bool DnCudaEngine::render(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels,
                          const NoiseProfile& profile, const SpatialParams& params, bool rgba, int view, float mix) {
  if (!ok()) return false;
  Backend& B = impl_->backend;
  B.err = cudaSuccess;
  if (!impl_->pipeline->run(src, dst, half, w, h, rowPixels, profile, params, rgba, view, mix)) return false;
  B.note(cudaStreamSynchronize(B.stream));
  return B.err == cudaSuccess;
}

bool DnCudaEngine::downloadCrop(const void* src, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, bool rgba,
                                bool bottomUp, uint32_t maxW, uint32_t maxH, vv::Image& out) {
  if (!ok() || !w || !h) return false;
  const uint32_t cw = std::min(w, maxW), ch = std::min(h, maxH), x0 = (w - cw) / 2, y0 = (h - ch) / 2;
  const size_t px = half ? 8 : 16;
  std::vector<unsigned char> host(size_t(cw) * ch * px);
  const unsigned char* base = static_cast<const unsigned char*>(src) + (size_t(y0) * rowPixels + x0) * px;
  if (cudaMemcpy2D(host.data(), cw * px, base, size_t(rowPixels) * px, cw * px, ch, cudaMemcpyDeviceToHost) != cudaSuccess)
    return false;
  out = vv::Image(int(cw), int(ch));
  for (uint32_t y = 0; y < ch; ++y)
    for (uint32_t x = 0; x < cw; ++x) {
      const size_t i = size_t(y) * cw + x;
      const F4 s = half ? loadPx(reinterpret_cast<const H4*>(host.data()), uint32_t(i))
                        : loadPx(reinterpret_cast<const F4*>(host.data()), uint32_t(i));
      float* d = out.at(int(x), int(bottomUp ? ch - 1 - y : y));
      d[0] = rgba ? s.x : s.z; d[1] = s.y; d[2] = rgba ? s.z : s.x; d[3] = s.w;
    }
  return true;
}
}  // namespace dn
