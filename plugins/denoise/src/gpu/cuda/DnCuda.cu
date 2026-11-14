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
__global__ void kPrepare(const T* src, F4* opp, FrameLayout f) {
  uint32_t x, y;
  if (coords(f.width, f.height, x, y)) pxDnPrepare(src, opp, f, x, y);
}
__global__ void kStarletH(const F4* s, F4* d, PlaneInfo p, int step) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxDnStarletH(s, d, p, step, x, y);
}
__global__ void kStarletVBand(const F4* t, const F4* c, F4* n, F4* b, F4* e, F4* v, DnProfileTable tab, BandInfo bi,
                              int step) {
  uint32_t x, y;
  if (coords(bi.width, bi.height, x, y)) pxDnStarletVBand(t, c, n, b, e, v, tab, bi, step, x, y);
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
  void dnPrepare(const void* src, bool half, Buf opp, const FrameLayout& f) {
    if (half) kPrepare<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const H4*>(src), opp, f);
    else kPrepare<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const F4*>(src), opp, f);
    check();
  }
  void dnStarletH(Buf s, Buf t, PlaneInfo p, int step) {
    kStarletH<<<grid(p.width, p.height), block(), 0, stream>>>(s, t, p, step);
    check();
  }
  void dnStarletVBand(Buf t, Buf c, Buf n, Buf b, Buf e, Buf v, const DnProfileTable& tab, const BandInfo& bi, int step) {
    kStarletVBand<<<grid(bi.width, bi.height), block(), 0, stream>>>(t, c, n, b, e, v, tab, bi, step);
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

bool DnCudaEngine::readRegion(const void* buf, bool half, uint32_t rowPixels, bool rgba, uint32_t x0, uint32_t y0,
                              uint32_t w, uint32_t h, vv::Image& out) {
  if (!ok() || !w || !h) return false;
  const size_t px = half ? 8 : 16;
  std::vector<unsigned char> host(size_t(w) * h * px);
  const unsigned char* base = static_cast<const unsigned char*>(buf) + (size_t(y0) * rowPixels + x0) * px;
  if (cudaMemcpy2D(host.data(), w * px, base, size_t(rowPixels) * px, w * px, h, cudaMemcpyDeviceToHost) != cudaSuccess)
    return false;
  out = vv::Image(int(w), int(h));
  for (uint32_t i = 0; i < w * h; ++i) {
    const F4 s = half ? loadPx(reinterpret_cast<const H4*>(host.data()), i) : loadPx(reinterpret_cast<const F4*>(host.data()), i);
    float* d = &out.px[size_t(i) * 4];
    d[0] = rgba ? s.x : s.z; d[1] = s.y; d[2] = rgba ? s.z : s.x; d[3] = s.w;
  }
  return true;
}

bool DnCudaEngine::writeRegion(void* buf, bool half, uint32_t rowPixels, bool rgba, uint32_t x0, uint32_t y0,
                               const vv::Image& in) {
  if (!ok() || in.width <= 0 || in.height <= 0) return false;
  const uint32_t w = uint32_t(in.width), h = uint32_t(in.height);
  const size_t px = half ? 8 : 16;
  std::vector<unsigned char> host(size_t(w) * h * px);
  for (uint32_t i = 0; i < w * h; ++i) {
    const float* s = &in.px[size_t(i) * 4];
    const F4 v = rgba ? F4{s[0], s[1], s[2], s[3]} : F4{s[2], s[1], s[0], s[3]};
    if (half) storePx(reinterpret_cast<H4*>(host.data()), i, v);
    else storePx(reinterpret_cast<F4*>(host.data()), i, v);
  }
  unsigned char* base = static_cast<unsigned char*>(buf) + (size_t(y0) * rowPixels + x0) * px;
  return cudaMemcpy2D(base, size_t(rowPixels) * px, host.data(), w * px, w * px, h, cudaMemcpyHostToDevice) == cudaSuccess;
}
}  // namespace dn
