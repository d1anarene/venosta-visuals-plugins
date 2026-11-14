#include "dn_starlet.h"

#include <algorithm>

#include "vv/parallel.h"

namespace dn {

vv::Plane starletSmooth(const vv::Plane& p, int j) {
  // B3 spline, separable, clamped borders. Same sums in the same order everywhere (bit-identical
  // to the straightforward loop); the interior skips the clamps.
  static const float k[5] = {1.f / 16, 4.f / 16, 6.f / 16, 4.f / 16, 1.f / 16};
  const int step = 1 << j, w = p.width, h = p.height, c = p.channels;
  vv::Plane tmp(w, h, c), out(w, h, c);
  vv::parallelRows(h, [&](int y) {
    const float* row = p.at(0, y);
    float* dst = tmp.at(0, y);
    for (int x = 0; x < w; ++x) {
      const bool inside = x - 2 * step >= 0 && x + 2 * step < w;
      for (int ch = 0; ch < c; ++ch) {
        float acc = 0;
        for (int t = -2; t <= 2; ++t) {
          const int xx = inside ? x + t * step : std::clamp(x + t * step, 0, w - 1);
          acc += k[t + 2] * row[size_t(xx) * c + ch];
        }
        dst[size_t(x) * c + ch] = acc;
      }
    }
  });
  vv::parallelRows(h, [&](int y) {
    const float* rows[5];
    for (int t = -2; t <= 2; ++t) rows[t + 2] = tmp.at(0, std::clamp(y + t * step, 0, h - 1));
    float* dst = out.at(0, y);
    const size_t n = size_t(w) * c;
    for (size_t i = 0; i < n; ++i) {
      float acc = 0;
      for (int t = 0; t < 5; ++t) acc += k[t] * rows[t][i];
      dst[i] = acc;
    }
  });
  return out;
}

Starlet starletDecompose(const vv::Plane& img, int levels) {
  Starlet s;
  vv::Plane cur = img;
  for (int j = 0; j < levels; ++j) {
    vv::Plane next = starletSmooth(cur, j);
    vv::Plane band = cur;
    for (size_t i = 0; i < band.v.size(); ++i) band.v[i] -= next.v[i];
    s.bands.push_back(std::move(band));
    s.smooth.push_back(next);
    cur = std::move(next);
  }
  return s;
}

vv::Plane starletReconstruct(const Starlet& s) {
  vv::Plane out = s.smooth.back();
  for (const vv::Plane& b : s.bands)
    for (size_t i = 0; i < out.v.size(); ++i) out.v[i] += b.v[i];
  return out;
}

}  // namespace dn
