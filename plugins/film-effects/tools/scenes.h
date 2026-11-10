// Synthetic, redistributable test scenes shared by tools/fe_render and tests/gpu.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/fe_kernels.h"
#include "image.h"

namespace fe {
namespace scenes {

inline void put(Image& im, int x, int y, float r, float g, float b) {
  if (x < 0 || y < 0 || x >= im.width || y >= im.height) return;
  float* p = im.at(x, y);
  p[0] = r; p[1] = g; p[2] = b;
}
inline void fillRect(Image& im, float x0, float y0, float x1, float y1, float r, float g, float b) {
  for (int y = (int)(y0 * im.height); y < (int)(y1 * im.height); ++y)
    for (int x = (int)(x0 * im.width); x < (int)(x1 * im.width); ++x) put(im, x, y, r, g, b);
}
inline void disc(Image& im, float cx, float cy, float rad, float r, float g, float b) {
  float R = rad * im.height;
  int X = (int)(cx * im.width), Y = (int)(cy * im.height);
  for (int y = (int)(Y - R - 1); y <= Y + R + 1; ++y)
    for (int x = (int)(X - R - 1); x <= X + R + 1; ++x) {
      float d = std::sqrt(float((x - X) * (x - X) + (y - Y) * (y - Y)));
      float a = std::clamp(R + 0.5f - d, 0.f, 1.f);
      if (a > 0 && x >= 0 && y >= 0 && x < im.width && y < im.height) {
        float* p = im.at(x, y);
        p[0] += (r - p[0]) * a; p[1] += (g - p[1]) * a; p[2] += (b - p[2]) * a;
      }
    }
}
inline float hashf(int x, int y) { return (fe_hash3((fe_uint)x, (fe_uint)y, 77u) & 0xffff) / 65535.f; }

inline Image makeScene(const std::string& name, int w, int h) {
  Image im(w, h);
  for (size_t i = 0; i < im.px.size(); i += 4) im.px[i + 3] = 1.f;
  if (name == "point-lights") {
    // Small lights of different colours and brightness on black (values in working encoding;
    // >1 = super-white highlights as produced by Lumetri/HDR sources).
    disc(im, 0.2f, 0.5f, 0.006f, 1.6f, 1.6f, 1.6f);
    disc(im, 0.4f, 0.5f, 0.006f, 1.3f, 1.25f, 1.1f);
    disc(im, 0.6f, 0.5f, 0.006f, 0.2f, 1.4f, 1.5f);   // cyan
    disc(im, 0.8f, 0.5f, 0.006f, 1.5f, 0.2f, 1.4f);   // magenta
    disc(im, 0.5f, 0.2f, 0.002f, 2.0f, 2.0f, 2.0f);   // tiny hot spot
  } else if (name == "window") {
    fillRect(im, 0, 0, 1, 1, 0.06f, 0.06f, 0.07f);
    fillRect(im, 0.3f, 0.2f, 0.7f, 0.8f, 1.15f, 1.12f, 1.05f);
    fillRect(im, 0.49f, 0.2f, 0.51f, 0.8f, 0.05f, 0.05f, 0.05f);  // window bar
    fillRect(im, 0.3f, 0.49f, 0.7f, 0.51f, 0.05f, 0.05f, 0.05f);
  } else if (name == "neon") {
    fillRect(im, 0, 0, 1, 1, 0.03f, 0.03f, 0.04f);
    for (int i = 0; i < 5; ++i) fillRect(im, 0.15f + i * 0.15f, 0.35f, 0.17f + i * 0.15f, 0.65f, 1.3f, 0.35f, 0.45f);
    fillRect(im, 0.15f, 0.70f, 0.85f, 0.71f, 0.5f, 1.2f, 1.4f);   // thin bright line
  } else if (name == "gradient") {
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float v = 1.6f * x / (w - 1);
        put(im, x, y, v, v, v);
      }
  } else if (name == "chart") {
    // 6x4 colour patches (approximate classic chart values, display-referred) + grey ramp.
    static const float c[24][3] = {
        {.45f, .32f, .26f}, {.76f, .58f, .50f}, {.36f, .48f, .61f}, {.35f, .42f, .25f}, {.51f, .50f, .69f}, {.39f, .74f, .67f},
        {.85f, .47f, .17f}, {.29f, .36f, .65f}, {.76f, .33f, .38f}, {.36f, .24f, .42f}, {.62f, .73f, .25f}, {.89f, .63f, .18f},
        {.17f, .24f, .58f}, {.27f, .58f, .29f}, {.69f, .20f, .22f}, {.93f, .78f, .12f}, {.73f, .33f, .58f}, {.00f, .52f, .65f},
        {.96f, .96f, .95f}, {.78f, .79f, .79f}, {.63f, .63f, .63f}, {.48f, .48f, .48f}, {.33f, .34f, .34f}, {.20f, .20f, .20f}};
    fillRect(im, 0, 0, 1, 1, 0.1f, 0.1f, 0.1f);
    for (int i = 0; i < 24; ++i) {
      float x0 = 0.05f + (i % 6) * 0.15f, y0 = 0.08f + (i / 6) * 0.21f;
      fillRect(im, x0, y0, x0 + 0.13f, y0 + 0.19f, c[i][0], c[i][1], c[i][2]);
    }
  } else if (name == "night-street") {
    // Dark scene with a sky gradient, a lit window row, street lamps and a skin-tone face patch.
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float t = (float)y / h;
        float n = 0.015f * (hashf(x / 3, y / 3) - 0.5f);
        put(im, x, y, 0.05f + 0.05f * (1 - t) + n, 0.06f + 0.05f * (1 - t) + n, 0.10f + 0.06f * (1 - t) + n);
      }
    fillRect(im, 0.05f, 0.25f, 0.45f, 0.75f, 0.12f, 0.11f, 0.10f);  // building
    for (int i = 0; i < 4; ++i) fillRect(im, 0.08f + i * 0.09f, 0.35f, 0.14f + i * 0.09f, 0.45f, 1.1f, 0.9f, 0.6f);
    for (int i = 0; i < 3; ++i) {
      fillRect(im, 0.55f + i * 0.15f, 0.30f, 0.555f + i * 0.15f, 0.9f, 0.08f, 0.08f, 0.08f);
      disc(im, 0.5525f + i * 0.15f, 0.29f, 0.012f, 1.8f, 1.5f, 1.0f);
    }
    disc(im, 0.25f, 0.85f, 0.08f, 0.62f, 0.45f, 0.36f);  // face-like skin patch (midtone)
  } else if (name == "foliage") {
    // Procedural high-frequency texture with mid-tones (stand-in for foliage detail).
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float v = 0;
        for (int o = 1; o <= 4; ++o) v += (hashf(x >> o, y >> o) - 0.5f) / o;
        put(im, x, y, 0.18f + 0.15f * v, 0.35f + 0.25f * v, 0.12f + 0.1f * v);
      }
  } else if (name == "midgrey") {
    fillRect(im, 0, 0, 1, 1, 0.46f, 0.46f, 0.46f);
  } else {
    std::fprintf(stderr, "unknown scene %s\n", name.c_str());
    std::exit(2);
  }
  return im;
}


inline const char* const kAll[] = {"point-lights", "window", "neon", "gradient", "chart", "night-street", "foliage", "midgrey"};

}  // namespace scenes
}  // namespace fe
