#include "dn_metrics.h"

#include <cmath>
#include <limits>

#include "vv/blur.h"

namespace dn {
namespace eval {

double psnr(const vv::Image& a, const vv::Image& r) {
  double se = 0;
  size_t n = 0;
  for (size_t i = 0; i < a.px.size(); ++i) {
    if (i % 4 == 3) continue;
    const double d = double(a.px[i]) - r.px[i];
    se += d * d; ++n;
  }
  const double mse = se / double(n);
  return mse > 0 ? 10.0 * std::log10(1.0 / mse) : std::numeric_limits<double>::infinity();
}

double ssim(const vv::Image& a, const vv::Image& r) {
  const int w = a.width, h = a.height;
  // Channels: x, y, x^2, y^2, xy.
  vv::Plane s(w, h, 5);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float* p = a.at(x, y);
      const float* q = r.at(x, y);
      const float lx = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
      const float ly = 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2];
      float* d = s.at(x, y);
      d[0] = lx; d[1] = ly; d[2] = lx * lx; d[3] = ly * ly; d[4] = lx * ly;
    }
  const vv::Plane m = vv::gaussianBlurDirect(s, 1.5f);
  const double c1 = 0.01 * 0.01, c2 = 0.03 * 0.03;
  double sum = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float* d = m.at(x, y);
      const double mx = d[0], my = d[1];
      const double vx = d[2] - mx * mx, vy = d[3] - my * my, cxy = d[4] - mx * my;
      sum += ((2 * mx * my + c1) * (2 * cxy + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2));
    }
  return sum / (double(w) * h);
}

}  // namespace eval
}  // namespace dn
