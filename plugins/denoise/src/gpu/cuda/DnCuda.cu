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
__global__ void kHalve(const T* src, F4* d, FrameLayout f, PlaneInfo hp) {
  uint32_t x, y;
  if (coords(hp.width, hp.height, x, y)) pxDnHalve(src, d, f, hp, x, y);
}
template <typename T>
__global__ void kUpFinish(const T* src, T* dst, const F4* low, FrameLayout f, PlaneInfo hp) {
  uint32_t x, y;
  if (coords(f.width, f.height, x, y)) pxDnUpFinish(src, dst, low, f, hp, x, y);
}
template <typename T>
__global__ void kCrop(const T* src, F4* rgb, CropInfo c) {
  uint32_t x, y;
  if (coords(c.width, c.height, x, y)) pxDnCrop(src, rgb, c, x, y);
}
__global__ void kStarletV(const F4* s, F4* d, PlaneInfo p, int step) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxDnStarletV(s, d, p, step, x, y);
}
__global__ void kBlocks(const F4* c, const F4* n, F4* b, BlockGrid g) {
  uint32_t q, r;
  if (coords(g.cols, g.rows, q, r)) pxDnBlock(c, n, b, g, q, r);
}
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
  static const void* ptr(Buf b) { return b; }
  static void* mptr(Buf b) { return b; }
  void dnHalve(const void* src, bool half, Buf d, const FrameLayout& f, PlaneInfo hp) {
    if (half) kHalve<<<grid(hp.width, hp.height), block(), 0, stream>>>(static_cast<const H4*>(src), d, f, hp);
    else kHalve<<<grid(hp.width, hp.height), block(), 0, stream>>>(static_cast<const F4*>(src), d, f, hp);
    check();
  }
  void dnUpFinish(const void* src, void* dst, bool half, Buf low, const FrameLayout& f, PlaneInfo hp) {
    if (half)
      kUpFinish<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const H4*>(src), static_cast<H4*>(dst), low, f, hp);
    else
      kUpFinish<<<grid(f.width, f.height), block(), 0, stream>>>(static_cast<const F4*>(src), static_cast<F4*>(dst), low, f, hp);
    check();
  }
  void dnCrop(const void* src, bool half, Buf rgb, const CropInfo& c) {
    if (half) kCrop<<<grid(c.width, c.height), block(), 0, stream>>>(static_cast<const H4*>(src), rgb, c);
    else kCrop<<<grid(c.width, c.height), block(), 0, stream>>>(static_cast<const F4*>(src), rgb, c);
    check();
  }
  void dnStarletV(Buf s, Buf d, PlaneInfo p, int step) {
    kStarletV<<<grid(p.width, p.height), block(), 0, stream>>>(s, d, p, step);
    check();
  }
  void dnBlocks(Buf c, Buf n, Buf b, size_t off, const BlockGrid& g) {
    kBlocks<<<grid(g.cols, g.rows), block(), 0, stream>>>(c, n, b + off, g);
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
                          const NoiseProfile& profile, const SpatialParams& params, bool rgba, int view, float mix,
                          bool preview) {
  if (!ok()) return false;
  Backend& B = impl_->backend;
  B.err = cudaSuccess;
  if (!impl_->pipeline->run(src, dst, half, w, h, rowPixels, profile, params, rgba, view, mix, preview)) return false;
  B.note(cudaStreamSynchronize(B.stream));
  return B.err == cudaSuccess;
}

bool DnCudaEngine::estimateBlocks(const void* src, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, bool rgba,
                                  bool bottomUp, uint32_t maxW, uint32_t maxH,
                                  std::vector<std::vector<EstimateBlock>>& out, int& cropW, int& cropH, bool preview) {
  if (!ok()) return false;
  Backend& B = impl_->backend;
  B.err = cudaSuccess;
  gpu::SpatialPipeline<Backend>& P = *impl_->pipeline;
  if (!P.encodeEstimate(src, half, w, h, rowPixels, rgba, bottomUp, maxW, maxH, preview)) return false;
  std::vector<F4> host(size_t(P.blockCount()) * 3 * DN_BANDS);
  B.note(cudaMemcpyAsync(host.data(), P.blocks(), host.size() * sizeof(F4), cudaMemcpyDeviceToHost, B.stream));
  B.note(cudaStreamSynchronize(B.stream));
  if (B.err != cudaSuccess) return false;
  out = gpu::unpackBlocks(host.data(), P.blockCount(), DN_BANDS);
  cropW = int(P.cropWidth());
  cropH = int(P.cropHeight());
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
