#include "dn_starlet.h"

#include <algorithm>

#include "vv/parallel.h"

namespace dn {

vv::Plane starletSmooth(const vv::Plane& p, int j) {
  static const float k[5] = {1.f / 16, 4.f / 16, 6.f / 16, 4.f / 16, 1.f / 16};
  const int step = 1 << j, w = p.width, h = p.height, c = p.channels;
  vv::Plane tmp(w, h, c), out(w, h, c);
  vv::parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x)
      for (int ch = 0; ch < c; ++ch) {
        float acc = 0;
        for (int t = -2; t <= 2; ++t) acc += k[t + 2] * p.at(std::clamp(x + t * step, 0, w - 1), y)[ch];
        tmp.at(x, y)[ch] = acc;
      }
  });
  vv::parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x)
      for (int ch = 0; ch < c; ++ch) {
        float acc = 0;
        for (int t = -2; t <= 2; ++t) acc += k[t + 2] * tmp.at(x, std::clamp(y + t * step, 0, h - 1))[ch];
        out.at(x, y)[ch] = acc;
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
