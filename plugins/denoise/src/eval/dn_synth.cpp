#include "dn_synth.h"

#include <algorithm>
#include <cmath>

#include "vv/blur.h"

namespace dn {
namespace eval {

float gauss(int x, int y, int k, uint32_t key) {
  const vv_uint h1 = vv_hash3(vv_uint(x), vv_uint(y), vv_hash(key ^ vv_uint(k) * 0x27d4eb2du));
  const vv_uint h2 = vv_hash(h1 + 0x9E3779B9u);
  const float u1 = (float(h1 >> 8) + 0.5f) / 16777216.f, u2 = float(h2 >> 8) / 16777216.f;
  return std::sqrt(-2.f * std::log(u1)) * std::cos(6.2831853f * u2);
}

namespace {

// Channel recorded at (x, y) in an RGGB mosaic.
int bayerChannel(int x, int y) { return (y & 1) == 0 ? ((x & 1) == 0 ? 0 : 1) : ((x & 1) == 0 ? 1 : 2); }

// Bilinear demosaic of a mosaic plane (1 channel) into RGB (3 channels).
vv::Plane demosaic(const vv::Plane& m) {
  const int w = m.width, h = m.height;
  vv::Plane o(w, h, 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) {
        if (bayerChannel(x, y) == c) { o.at(x, y)[c] = m.at(x, y)[0]; continue; }
        double s = 0;
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= w || yy >= h || bayerChannel(xx, yy) != c) continue;
            // Green at a red/blue site: the 4 direct neighbours; red/blue: 2 or 4 nearest of that colour.
            if (c == 1 && dx != 0 && dy != 0) continue;
            s += m.at(xx, yy)[0]; ++n;
          }
        o.at(x, y)[c] = n ? float(s / n) : m.at(x, y)[0];
      }
  return o;
}

// Typical camera-RGB -> output-RGB matrix (rows sum to 1: white stays white) and its inverse.
const float kM[3][3] = {{1.70f, -0.55f, -0.15f}, {-0.25f, 1.55f, -0.30f}, {0.00f, -0.55f, 1.55f}};

void applyMatrix(vv::Plane& p, const float m[3][3]) {
  for (size_t i = 0, n = size_t(p.width) * p.height; i < n; ++i) {
    float* q = &p.v[i * 3];
    const float r = q[0], g = q[1], b = q[2];
    for (int c = 0; c < 3; ++c) q[c] = m[c][0] * r + m[c][1] * g + m[c][2] * b;
  }
}

void invert(const float m[3][3], float o[3][3]) {
  const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                     m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      const int a = (j + 1) % 3, b = (j + 2) % 3, c = (i + 1) % 3, d = (i + 2) % 3;
      o[i][j] = float((m[a][c] * m[b][d] - m[a][d] * m[b][c]) / det);
    }
}

vv::Plane sharpen(const vv::Plane& p, float amount) {
  if (amount <= 0.f) return p;
  const vv::Plane b = vv::gaussianBlurDirect(p, 1.0f);
  vv::Plane o = p;
  for (size_t i = 0; i < o.v.size(); ++i) o.v[i] += amount * (p.v[i] - b.v[i]);
  return o;
}

vv::Image encode(const vv::Plane& lin, const vv::Image& alpha, int space) {
  vv::Image o(lin.width, lin.height);
  for (int y = 0; y < lin.height; ++y)
    for (int x = 0; x < lin.width; ++x)
      for (int c = 0; c < 4; ++c)
        o.at(x, y)[c] = c < 3 ? vv_from_linear1(lin.at(x, y)[c], space) : alpha.at(x, y)[3];
  return o;
}

}  // namespace

Synth synthesize(const vv::Image& src, const NoiseSim& sim, uint32_t seed, int64_t frame) {
  const int w = src.width, h = src.height;
  const uint32_t key = vv_hash3(seed, uint32_t(frame & 0xffffffff), uint32_t(uint64_t(frame) >> 32));
  vv::Plane lin(w, h, 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) lin.at(x, y)[c] = vv_to_linear1(src.at(x, y)[c], sim.space);
  float inv[3][3];
  invert(kM, inv);
  if (sim.matrix) applyMatrix(lin, inv);
  auto noisyValue = [&](float L, int x, int y, int c) {
    const float var = std::max(sim.shot * L, 0.f) + sim.read * sim.read;
    return L + std::sqrt(var) * gauss(x, y, c, key);
  };
  vv::Plane cleanLin, noisyLin;
  if (sim.bayer) {
    vv::Plane mc(w, h, 1), mn(w, h, 1);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const int c = bayerChannel(x, y);
        const float L = lin.at(x, y)[c];
        mc.at(x, y)[0] = L;
        mn.at(x, y)[0] = noisyValue(L, x, y, 0);
      }
    cleanLin = demosaic(mc);
    noisyLin = demosaic(mn);
  } else {
    cleanLin = lin;
    noisyLin = lin;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        for (int c = 0; c < 3; ++c) noisyLin.at(x, y)[c] = noisyValue(lin.at(x, y)[c], x, y, c);
  }
  if (sim.matrix) {
    applyMatrix(cleanLin, kM);
    applyMatrix(noisyLin, kM);
  }
  return {encode(sharpen(noisyLin, sim.sharpen), src, sim.space), encode(sharpen(cleanLin, sim.sharpen), src, sim.space)};
}

}  // namespace eval
}  // namespace dn
