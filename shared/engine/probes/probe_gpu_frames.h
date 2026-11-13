// Research-only: Premiere GPU frames (BGRA float32 or float16, padded rows) <-> vv::Image (RGBA
// float, straight alpha), on the host. Used by the CUDA probe after copying a frame off the GPU;
// pure C++ so it is tested without a GPU or an Adobe SDK.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "gpu/vv_gpu_pixels.h"
#include "vv/image.h"

namespace vv { namespace probe {

inline size_t gpuPixelBytes(bool half) { return half ? 8u : 16u; }

inline Image fromGpuBGRA(const void* host, size_t rowBytes, int width, int height, bool half) {
  if (!host || width <= 0 || height <= 0 || rowBytes < size_t(width) * gpuPixelBytes(half))
    throw std::invalid_argument("bad GPU frame layout");
  Image out(width, height);
  for (int y = 0; y < height; ++y) {
    const unsigned char* row = static_cast<const unsigned char*>(host) + size_t(y) * rowBytes;
    for (int x = 0; x < width; ++x) {
      float bgra[4];
      if (half) {
        uint16_t h[4];
        std::memcpy(h, row + size_t(x) * 8, 8);
        for (int c = 0; c < 4; ++c) bgra[c] = vv::gpu::halfToFloat(h[c]);
      } else {
        std::memcpy(bgra, row + size_t(x) * 16, 16);
      }
      float* p = out.at(x, y);
      p[0] = bgra[2]; p[1] = bgra[1]; p[2] = bgra[0]; p[3] = bgra[3];
    }
  }
  return out;
}

/** Writes the image's pixels; padding bytes after each row are left untouched. */
inline void toGpuBGRA(const Image& img, void* host, size_t rowBytes, bool half) {
  if (!host || rowBytes < size_t(img.width) * gpuPixelBytes(half)) throw std::invalid_argument("bad GPU frame layout");
  for (int y = 0; y < img.height; ++y) {
    unsigned char* row = static_cast<unsigned char*>(host) + size_t(y) * rowBytes;
    for (int x = 0; x < img.width; ++x) {
      const float* p = img.at(x, y);
      const float bgra[4] = {p[2], p[1], p[0], p[3]};
      if (half) {
        uint16_t h[4];
        for (int c = 0; c < 4; ++c) h[c] = vv::gpu::floatToHalf(bgra[c]);
        std::memcpy(row + size_t(x) * 8, h, 8);
      } else {
        std::memcpy(row + size_t(x) * 16, bgra, 16);
      }
    }
  }
}

}  // namespace probe
}  // namespace vv
