#pragma once
#include <cstddef>
#include <vector>

namespace vv {

/** Straight-alpha RGBA float image, tightly packed, top row first. */
struct Image {
  int width = 0;
  int height = 0;
  std::vector<float> px;  // RGBA interleaved

  Image() = default;
  Image(int w, int h) : width(w), height(h), px(static_cast<size_t>(w) * h * 4, 0.0f) {}
  float* at(int x, int y) { return &px[(static_cast<size_t>(y) * width + x) * 4]; }
  const float* at(int x, int y) const { return &px[(static_cast<size_t>(y) * width + x) * 4]; }
  bool empty() const { return width <= 0 || height <= 0; }
};

/** Single- or three-channel float plane for intermediate passes. */
struct Plane {
  int width = 0;
  int height = 0;
  int channels = 1;
  std::vector<float> v;

  Plane() = default;
  Plane(int w, int h, int c) : width(w), height(h), channels(c), v(static_cast<size_t>(w) * h * c, 0.0f) {}
  float* at(int x, int y) { return &v[(static_cast<size_t>(y) * width + x) * channels]; }
  const float* at(int x, int y) const { return &v[(static_cast<size_t>(y) * width + x) * channels]; }
};

}  // namespace vv
