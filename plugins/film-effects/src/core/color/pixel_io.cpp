#include "pixel_io.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fe {
namespace {

inline const uint8_t* row(const void* base, ptrdiff_t stride, int y) {
  return static_cast<const uint8_t*>(base) + stride * y;
}
inline uint8_t* row(void* base, ptrdiff_t stride, int y) {
  return static_cast<uint8_t*>(base) + stride * y;
}
inline uint8_t to8(float v) {
  if (!(v > 0.0f)) return 0;  // also maps NaN to 0
  if (v >= 1.0f) return 255;
  return static_cast<uint8_t>(std::lround(v * 255.0f));
}
inline float clean(float v) { return std::isfinite(v) ? v : 0.0f; }
inline uint16_t to16(float v) {
  if (!(v > 0.0f)) return 0;
  if (v >= 1.0f) return 32768;
  return static_cast<uint16_t>(std::lround(v * 32768.0f));
}

}  // namespace

void readPixels(const void* base, ptrdiff_t stride, int w, int h, PixelLayout layout, Image& out) {
  out = Image(w, h);
  for (int y = 0; y < h; ++y) {
    const uint8_t* r = row(base, stride, y);
    float* o = out.at(0, y);
    for (int x = 0; x < w; ++x, o += 4) {
      switch (layout) {
        case PixelLayout::BGRA_8u: {
          const uint8_t* p = r + x * 4;
          o[0] = p[2] / 255.0f; o[1] = p[1] / 255.0f; o[2] = p[0] / 255.0f; o[3] = p[3] / 255.0f;
          break;
        }
        case PixelLayout::ARGB_8u: {
          const uint8_t* p = r + x * 4;
          float a = p[0] / 255.0f;
          float inv = p[0] ? 255.0f / p[0] : 0.0f;
          o[0] = std::min(p[1] * inv / 255.0f, 1.0f);
          o[1] = std::min(p[2] * inv / 255.0f, 1.0f);
          o[2] = std::min(p[3] * inv / 255.0f, 1.0f);
          o[3] = a;
          break;
        }
        case PixelLayout::BGRA_32f: {
          float p[4];
          std::memcpy(p, r + x * 16, 16);
          o[0] = clean(p[2]); o[1] = clean(p[1]); o[2] = clean(p[0]); o[3] = clean(p[3]);
          break;
        }
        case PixelLayout::RGBA_32f: {
          float p[4];
          std::memcpy(p, r + x * 16, 16);
          o[0] = clean(p[0]); o[1] = clean(p[1]); o[2] = clean(p[2]); o[3] = clean(p[3]);
          break;
        }
        case PixelLayout::ARGB_16u: {
          uint16_t p[4];
          std::memcpy(p, r + x * 8, 8);
          float a = p[0] / 32768.0f;
          float inv = p[0] ? 1.0f / p[0] : 0.0f;
          o[0] = std::min(p[1] * inv, 1.0f); o[1] = std::min(p[2] * inv, 1.0f); o[2] = std::min(p[3] * inv, 1.0f);
          o[3] = a;
          break;
        }
        case PixelLayout::ARGB_32f: {
          float p[4];
          std::memcpy(p, r + x * 16, 16);
          float a = clean(p[0]);
          float inv = a > 0.0f ? 1.0f / a : 0.0f;
          o[0] = clean(p[1]) * inv; o[1] = clean(p[2]) * inv; o[2] = clean(p[3]) * inv; o[3] = a;
          break;
        }
      }
    }
  }
}

void writePixels(const Image& img, void* base, ptrdiff_t stride, PixelLayout layout,
                 const void* aBase, ptrdiff_t aStride) {
  for (int y = 0; y < img.height; ++y) {
    uint8_t* r = row(base, stride, y);
    const uint8_t* ar = aBase ? row(aBase, aStride, y) : nullptr;
    const float* s = img.at(0, y);
    for (int x = 0; x < img.width; ++x, s += 4) {
      switch (layout) {
        case PixelLayout::BGRA_8u: {
          uint8_t* p = r + x * 4;
          p[0] = to8(s[2]); p[1] = to8(s[1]); p[2] = to8(s[0]);
          p[3] = ar ? ar[x * 4 + 3] : to8(s[3]);
          break;
        }
        case PixelLayout::ARGB_8u: {
          uint8_t* p = r + x * 4;
          uint8_t a8 = ar ? ar[x * 4 + 0] : to8(s[3]);
          float a = a8 / 255.0f;
          p[0] = a8;
          p[1] = to8(s[0] * a); p[2] = to8(s[1] * a); p[3] = to8(s[2] * a);
          break;
        }
        case PixelLayout::BGRA_32f: {
          float p[4] = {s[2], s[1], s[0], s[3]};
          if (ar) std::memcpy(&p[3], ar + x * 16 + 12, 4);
          std::memcpy(r + x * 16, p, 16);
          break;
        }
        case PixelLayout::RGBA_32f: {
          float p[4] = {s[0], s[1], s[2], s[3]};
          if (ar) std::memcpy(&p[3], ar + x * 16 + 12, 4);
          std::memcpy(r + x * 16, p, 16);
          break;
        }
        case PixelLayout::ARGB_16u: {
          uint16_t p[4];
          if (ar) std::memcpy(&p[0], ar + x * 8, 2);
          else p[0] = to16(s[3]);
          float a = p[0] / 32768.0f;
          p[1] = to16(s[0] * a); p[2] = to16(s[1] * a); p[3] = to16(s[2] * a);
          std::memcpy(r + x * 8, p, 8);
          break;
        }
        case PixelLayout::ARGB_32f: {
          float p[4];
          if (ar) std::memcpy(&p[0], ar + x * 16, 4);
          else p[0] = s[3];
          p[1] = s[0] * p[0]; p[2] = s[1] * p[0]; p[3] = s[2] * p[0];
          std::memcpy(r + x * 16, p, 16);
          break;
        }
      }
    }
  }
}

}  // namespace fe
