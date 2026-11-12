// Denoise evaluation — procedural, redistributable test images with the kinds of detail a
// denoiser must keep: fine periodic weave, multi-scale foliage, thin strokes ("text"), smooth
// skin-like shading, a full-range gradient, plus the engine's synthetic scenes.
#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "vv/image.h"
#include "vv/kernels.h"
#include "vv/scenes.h"

namespace dn {
namespace eval {

inline float fbm(float x, float y, vv_uint key, int octaves) {
  float s = 0, a = 0.5f, f = 1;
  for (int o = 0; o < octaves; ++o) {
    s += a * vv_value_noise(x * f, y * f, key + vv_uint(o) * 101u);
    a *= 0.5f; f *= 2.f;
  }
  return s;
}

inline vv::Image makeTestImage(const std::string& name, int w, int h) {
  vv::Image im(w, h);
  const float sc = float(h) / 540.f;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float r = 0, g = 0, b = 0;
      const float u = float(x) / sc, v = float(y) / sc;
      if (name == "weave") {
        const float t = 0.5f + 0.25f * std::sin(u * 1.9f) * std::sin(v * 1.9f) + 0.08f * fbm(u / 40, v / 40, 3u, 4);
        r = t * 0.75f; g = t * 0.55f; b = t * 0.45f;
      } else if (name == "foliage") {
        const float t = 0.35f + 0.18f * fbm(u / 6, v / 6, 7u, 5);
        r = t * 0.5f; g = t * 1.1f; b = t * 0.35f;
      } else if (name == "text") {
        // Rows of pseudo-glyphs made of 1-3 px strokes.
        const int row = int(v / 28), col = int(u / 16);
        const float lx = std::fmod(u, 16.f), ly = std::fmod(v, 28.f);
        const vv_uint hh = vv_hash3(vv_uint(row), vv_uint(col), 11u);
        bool ink = false;
        if (ly > 6 && ly < 22 && lx > 2 && lx < 13 && (hh & 3u) != 0) {
          ink = ((hh >> 2) & 1u) && lx < 4.5f;              // vertical stroke
          ink = ink || (((hh >> 3) & 1u) && ly > 13 && ly < 15.5f);  // bar
          ink = ink || (((hh >> 4) & 1u) && lx > 10 && lx < 11.5f);  // thin stroke
          ink = ink || (((hh >> 5) & 1u) && ly > 19.5f && ly < 21.f);
        }
        const float t = ink ? 0.08f : 0.85f;
        r = g = b = t;
      } else if (name == "skin") {
        const float t = 0.55f + 0.2f * std::sin(u / 90.f) * std::cos(v / 70.f) + 0.02f * fbm(u / 25, v / 25, 5u, 3);
        r = t * 0.95f; g = t * 0.72f; b = t * 0.6f;
      } else if (name == "ramp") {
        const float t = float(x) / float(w - 1);
        r = g = b = t;
        if (y > h / 2) { r = t; g = t * 0.6f; b = 1 - t; }
      } else {
        return vv::scenes::makeScene(name, w, h);
      }
      float* p = im.at(x, y);
      p[0] = std::fmin(std::fmax(r, 0.f), 1.f); p[1] = std::fmin(std::fmax(g, 0.f), 1.f);
      p[2] = std::fmin(std::fmax(b, 0.f), 1.f); p[3] = 1.f;
    }
  return im;
}

inline const std::vector<std::string>& testImageNames() {
  static const std::vector<std::string> n = {"weave", "foliage", "text", "skin", "ramp", "chart", "night-street"};
  return n;
}

}  // namespace eval
}  // namespace dn
