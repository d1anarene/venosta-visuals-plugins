#include "dn_pyramid.h"

#include "vv/blur.h"

namespace dn {

Pyramid decompose(const vv::Plane& img, int levels) {
  Pyramid p;
  vv::Plane cur = img;
  for (int b = 0; b < levels; ++b) {
    if (cur.width < 2 || cur.height < 2) break;
    vv::Plane next = vv::downsample2(cur);
    vv::Plane up = vv::upsampleTo(next, cur.width, cur.height);
    for (size_t i = 0; i < cur.v.size(); ++i) up.v[i] = cur.v[i] - up.v[i];
    p.bands.push_back(std::move(up));
    cur = std::move(next);
  }
  p.bands.push_back(std::move(cur));
  return p;
}

vv::Plane reconstruct(const Pyramid& p) {
  vv::Plane cur = p.bands.back();
  for (int b = int(p.bands.size()) - 2; b >= 0; --b) {
    const vv::Plane& band = p.bands[size_t(b)];
    vv::Plane up = vv::upsampleTo(cur, band.width, band.height);
    for (size_t i = 0; i < up.v.size(); ++i) up.v[i] += band.v[i];
    cur = std::move(up);
  }
  return cur;
}

vv::Plane bandLowpass(const vv::Plane& img, int band) {
  vv::Plane cur = img;
  int w = img.width, h = img.height;
  for (int b = 0; b <= band; ++b) {
    if (b < band) { w = (w + 1) / 2; h = (h + 1) / 2; }
    cur = vv::downsample2(cur);
  }
  return vv::upsampleTo(cur, w, h);
}

}  // namespace dn
