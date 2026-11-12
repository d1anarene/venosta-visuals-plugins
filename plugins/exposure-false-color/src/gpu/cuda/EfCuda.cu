#include <cuda_runtime.h>

#include "EfCuda.h"
#include "ef_gpu_pixels.h"

namespace ef {
namespace {
template <typename T>
__global__ void kEf(const T* src, T* dst, EfInfo info, gpu::FrameLayout f) {
  const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < info.width && y < info.height) gpu::pxEf(src, dst, info, f, x, y);
}
}  // namespace

bool cudaRender(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, const Params& raw,
                bool rgba, bool bottomUp) {
  const Params p = sanitized(raw);
  if (!p.anyActive()) {
    if (src != dst && cudaMemcpy(dst, src, size_t(rowPixels) * h * (half ? 8 : 16), cudaMemcpyDeviceToDevice) != cudaSuccess)
      return false;
    return true;
  }
  const EfInfo info = ef::info(p, int(w), int(h));
  const gpu::FrameLayout f{rowPixels, rgba ? 1 : 0, bottomUp ? 1 : 0};
  const dim3 block(16, 16), grid((w + 15) / 16, (h + 15) / 16);
  if (half) kEf<<<grid, block>>>(static_cast<const gpu::H4*>(src), static_cast<gpu::H4*>(dst), info, f);
  else kEf<<<grid, block>>>(static_cast<const gpu::F4*>(src), static_cast<gpu::F4*>(dst), info, f);
  return cudaGetLastError() == cudaSuccess && cudaStreamSynchronize(nullptr) == cudaSuccess;
}

}  // namespace ef
