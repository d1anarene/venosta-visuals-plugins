// Venosta Visuals engine — CUDA backend base for vv::gpu::Pyramid and plugin pipelines. Include in
// a plugin's .cu file; plugins derive from CudaBackendBase and add their own kernels/passes.
// Kernels are thin wrappers around gpu/vv_gpu_pixels.h. Work is queued on `stream` (default
// stream when null); the caller synchronises before returning a frame to the host.
#pragma once
#include <cuda_runtime.h>

#include <cstring>
#include <vector>

#include "gpu/vv_gpu_pixels.h"

namespace vv {
namespace gpu {
namespace cuda {

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

static __global__ void kDownsample(const F4* s, F4* d, PlaneInfo si, PlaneInfo di) {
  uint32_t x, y;
  if (coords(di.width, di.height, x, y)) pxDownsample(s, d, si, di, x, y);
}
static __global__ void kDownsampleX(const F4* s, F4* d, PlaneInfo si, PlaneInfo di) {
  uint32_t x, y;
  if (coords(di.width, di.height, x, y)) pxDownsampleX(s, d, si, di, x, y);
}
static __global__ void kBlurH(const F4* s, F4* d, PlaneInfo p, Taps t) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxBlurH(s, d, p, t.w, t.radius, x, y);
}
static __global__ void kBlurV(const F4* s, F4* d, PlaneInfo p, Taps t) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxBlurV(s, d, p, t.w, t.radius, x, y);
}
static __global__ void kBlurHBig(const F4* s, F4* d, PlaneInfo p, const float* w, int radius) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxBlurH(s, d, p, w, radius, x, y);
}
static __global__ void kBlurVBig(const F4* s, F4* d, PlaneInfo p, const float* w, int radius) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxBlurV(s, d, p, w, radius, x, y);
}
static __global__ void kUpsample(const F4* s, F4* d, PlaneInfo si, PlaneInfo di) {
  uint32_t x, y;
  if (coords(di.width, di.height, x, y)) pxUpsample(s, d, si, di, x, y);
}
static __global__ void kUpsampleX(const F4* s, F4* d, PlaneInfo si, PlaneInfo di) {
  uint32_t x, y;
  if (coords(di.width, di.height, x, y)) pxUpsampleX(s, d, si, di, x, y);
}
static __global__ void kAxpy(F4* acc, const F4* v, float w, PlaneInfo p) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxAxpy(acc, v, w, p, x, y);
}
static __global__ void kClear(F4* d, PlaneInfo p) {
  uint32_t x, y;
  if (coords(p.width, p.height, x, y)) pxClear(d, p, x, y);
}

struct CudaBackendBase {
  using Buf = F4*;
  cudaStream_t stream = nullptr;
  std::vector<void*> owned;
  cudaError_t err = cudaSuccess;

  ~CudaBackendBase() { releaseAll(); }
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

  void downsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    kDownsample<<<grid(di.width, di.height), block(), 0, stream>>>(s, d, si, di);
    check();
  }
  void downsampleX(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    kDownsampleX<<<grid(di.width, di.height), block(), 0, stream>>>(s, d, si, di);
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
    } else {  // only for extreme sizes; slow path with a temporary weights buffer
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
  void upsampleX(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    kUpsampleX<<<grid(di.width, di.height), block(), 0, stream>>>(s, d, si, di);
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
};

}  // namespace cuda
}  // namespace gpu
}  // namespace vv
