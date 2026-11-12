#include "blur.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "parallel.h"

namespace vv {
namespace {
// Above this sigma the blur runs on a half-resolution copy (sigma/2) and is upsampled.
constexpr float kPyramidSigma = 6.0f;

std::vector<float> kernelImpl(float sigma) {
  int r = std::max(1, static_cast<int>(std::ceil(4.0f * sigma)));  // 4 sigma: no visible cut-off ring around HDR lights
  std::vector<float> k(2 * r + 1);
  float sum = 0;
  for (int i = -r; i <= r; ++i) sum += (k[i + r] = std::exp(-0.5f * i * i / (sigma * sigma)));
  for (float& v : k) v /= sum;
  return k;
}
}  // namespace

std::vector<float> gaussianKernel(float sigma) { return kernelImpl(sigma); }

Plane gaussianBlurDirect(const Plane& src, float sigma) {
  if (sigma < 0.05f) return src;
  const auto k = gaussianKernel(sigma);
  const int r = static_cast<int>(k.size() / 2);
  const int w = src.width, h = src.height, c = src.channels;
  Plane tmp(w, h, c), dst(w, h, c);
  parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      float acc[3] = {0, 0, 0};
      for (int i = -r; i <= r; ++i) {
        const float* s = src.at(std::clamp(x + i, 0, w - 1), y);
        for (int ch = 0; ch < c; ++ch) acc[ch] += s[ch] * k[i + r];
      }
      for (int ch = 0; ch < c; ++ch) tmp.at(x, y)[ch] = acc[ch];
    }
  });
  // Vertical pass, row-oriented: each output row is a weighted sum of whole input rows
  // (contiguous memory, auto-vectorisable). Same arithmetic order per pixel -> deterministic.
  const size_t rowLen = static_cast<size_t>(w) * c;
  parallelRows(h, [&](int y) {
    float* out = dst.at(0, y);
    std::fill(out, out + rowLen, 0.0f);
    for (int i = -r; i <= r; ++i) {
      const float* in = tmp.at(0, std::clamp(y + i, 0, h - 1));
      const float kv = k[i + r];
      for (size_t j = 0; j < rowLen; ++j) out[j] += in[j] * kv;
    }
  });
  return dst;
}

void gaussianBlurAccumulate(const Plane& src, float sigma, float weight, Plane& acc) {
  // Same arithmetic as gaussianBlurDirect (horizontal pass, then vertical pass summed in the same
  // tap order); the vertical result is scaled and added instead of stored.
  const size_t rowLen = static_cast<size_t>(src.width) * src.channels;
  if (sigma < 0.05f) {
    for (size_t i = 0; i < acc.v.size(); ++i) acc.v[i] += weight * src.v[i];
    return;
  }
  const auto k = gaussianKernel(sigma);
  const int r = static_cast<int>(k.size() / 2);
  const int w = src.width, h = src.height, c = src.channels;
  Plane tmp(w, h, c);
  parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      float a[3] = {0, 0, 0};
      for (int i = -r; i <= r; ++i) {
        const float* s = src.at(std::clamp(x + i, 0, w - 1), y);
        for (int ch = 0; ch < c; ++ch) a[ch] += s[ch] * k[i + r];
      }
      for (int ch = 0; ch < c; ++ch) tmp.at(x, y)[ch] = a[ch];
    }
  });
  parallelRows(h, [&](int y) {
    std::vector<float> row(rowLen, 0.0f);
    for (int i = -r; i <= r; ++i) {
      const float* in = tmp.at(0, std::clamp(y + i, 0, h - 1));
      const float kv = k[i + r];
      for (size_t j = 0; j < rowLen; ++j) row[j] += in[j] * kv;
    }
    float* out = acc.at(0, y);
    for (size_t j = 0; j < rowLen; ++j) out[j] += weight * row[j];
  });
}

Plane downsample2(const Plane& src) {
  // Separable [1 3 3 1]/8 filter, decimated by 2: much less aliasing (and less shimmer on moving
  // highlights) than a plain 2x2 box. Output pixel i is centred on input coordinate 2i + 0.5.
  const int w = std::max(1, (src.width + 1) / 2), h = std::max(1, (src.height + 1) / 2);
  static const float k[4] = {0.125f, 0.375f, 0.375f, 0.125f};
  const int c = src.channels;
  Plane tmp(w, src.height, c), d(w, h, c);
  for (int y = 0; y < src.height; ++y)
    for (int x = 0; x < w; ++x)
      for (int ch = 0; ch < c; ++ch) {
        float acc = 0;
        for (int t = 0; t < 4; ++t) acc += k[t] * src.at(std::clamp(2 * x - 1 + t, 0, src.width - 1), y)[ch];
        tmp.at(x, y)[ch] = acc;
      }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int ch = 0; ch < c; ++ch) {
        float acc = 0;
        for (int t = 0; t < 4; ++t) acc += k[t] * tmp.at(x, std::clamp(2 * y - 1 + t, 0, src.height - 1))[ch];
        d.at(x, y)[ch] = acc;
      }
  return d;
}

Plane upsampleTo(const Plane& src, int w, int h) {
  Plane d(w, h, src.channels);
  // Exact inverse of downsample2's grid: full-res pixel centre x+0.5 maps to half-res (x+0.5)/2 - 0.5
  // (independent of odd/even sizes, so the pyramid introduces no shift).
  const float sx = 0.5f, sy = 0.5f;
  for (int y = 0; y < h; ++y) {
    float fy = std::clamp((y + 0.5f) * sy - 0.5f, 0.0f, static_cast<float>(src.height - 1));
    int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, src.height - 1);
    float ty = fy - y0;
    for (int x = 0; x < w; ++x) {
      float fx = std::clamp((x + 0.5f) * sx - 0.5f, 0.0f, static_cast<float>(src.width - 1));
      int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, src.width - 1);
      float tx = fx - x0;
      for (int ch = 0; ch < src.channels; ++ch) {
        float a = src.at(x0, y0)[ch], b = src.at(x1, y0)[ch], c = src.at(x0, y1)[ch], e = src.at(x1, y1)[ch];
        d.at(x, y)[ch] = (a + (b - a) * tx) + ((c + (e - c) * tx) - (a + (b - a) * tx)) * ty;
      }
    }
  }
  return d;
}

Plane gaussianBlur(const Plane& src, float sigma) {
  if (sigma <= kPyramidSigma || src.width < 8 || src.height < 8) return gaussianBlurDirect(src, sigma);
  // The [1 3 3 1] prefilter has variance 0.75 px^2 and bilinear upsampling adds ~0.5 px^2 more
  // (both at full resolution); the half-resolution sigma is reduced accordingly.
  Plane half = downsample2(src);
  float s2 = std::sqrt(std::max(sigma * sigma - 1.25f, 0.25f)) * 0.5f;
  return upsampleTo(gaussianBlur(half, s2), src.width, src.height);
}

}  // namespace vv

namespace vv {
Plane blurByPlan(const Plane& src, const BlurStep& step) {
  std::vector<Plane> chain;
  chain.push_back(src);
  for (int L = 1; L <= step.level; ++L) chain.push_back(downsample2(chain.back()));
  Plane cur = gaussianBlurDirect(chain.back(), step.sigmaAtLevel);
  for (int L = step.level - 1; L >= 0; --L) cur = upsampleTo(cur, chain[L].width, chain[L].height);
  return cur;
}
}  // namespace vv

namespace vv {

Plane downsampleX2(const Plane& src) {
  const int w = std::max(1, (src.width + 1) / 2);
  static const float k[4] = {0.125f, 0.375f, 0.375f, 0.125f};
  const int c = src.channels;
  Plane d(w, src.height, c);
  parallelRows(src.height, [&](int y) {
    for (int x = 0; x < w; ++x)
      for (int ch = 0; ch < c; ++ch) {
        float acc = 0;
        for (int t = 0; t < 4; ++t) acc += k[t] * src.at(std::clamp(2 * x - 1 + t, 0, src.width - 1), y)[ch];
        d.at(x, y)[ch] = acc;
      }
  });
  return d;
}

Plane upsampleXTo(const Plane& src, int w) {
  Plane d(w, src.height, src.channels);
  parallelRows(src.height, [&](int y) {
    for (int x = 0; x < w; ++x) {
      // Same grid as upsampleTo: full-res x+0.5 maps to half-res (x+0.5)/2 - 0.5.
      float fx = std::clamp((x + 0.5f) * 0.5f - 0.5f, 0.0f, static_cast<float>(src.width - 1));
      int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, src.width - 1);
      float tx = fx - x0;
      for (int ch = 0; ch < src.channels; ++ch) {
        float a = src.at(x0, y)[ch], b = src.at(x1, y)[ch];
        d.at(x, y)[ch] = a + (b - a) * tx;
      }
    }
  });
  return d;
}

namespace {
Plane blurRowsDirect(const Plane& src, float sigma) {
  if (sigma < 0.05f) return src;
  const std::vector<float> k = gaussianKernel(sigma);
  const int r = static_cast<int>(k.size() / 2), c = src.channels;
  Plane d(src.width, src.height, c);
  parallelRows(src.height, [&](int y) {
    for (int x = 0; x < src.width; ++x)
      for (int ch = 0; ch < c; ++ch) {
        float acc = 0;
        for (int i = -r; i <= r; ++i) acc += k[i + r] * src.at(std::clamp(x + i, 0, src.width - 1), y)[ch];
        d.at(x, y)[ch] = acc;
      }
  });
  return d;
}
}  // namespace

Plane gaussianBlurHorizontal(const Plane& src, float sigma) {
  const BlurStep step = planGaussianHorizontal(sigma, src.width);
  std::vector<Plane> levels;
  levels.push_back(src);
  for (int L = 1; L <= step.level; ++L) levels.push_back(downsampleX2(levels.back()));
  Plane cur = blurRowsDirect(levels.back(), step.sigmaAtLevel);
  for (int L = step.level - 1; L >= 0; --L) cur = upsampleXTo(cur, levels[L].width);
  return cur;
}

}  // namespace vv
