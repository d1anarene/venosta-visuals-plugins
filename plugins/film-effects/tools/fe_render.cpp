// fe_render — development-only CLI around the Film Effects core. NOT the Premiere plugin.
//
//   fe_render --scene <name> [--size WxH] [--preset "<name>"] [--set key=value ...]
//             [--frame N] [--out out.ppm] [--before before.ppm] [--bench N]
//
// Scenes are synthetic and generated here (fully redistributable). Output is 8-bit PPM in the
// working encoding (Rec.709 gamma 2.4 by default), which is what Premiere would display.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common/fe_kernels.h"
#include "pipeline/pipeline.h"

using namespace fe;

namespace {

void put(Image& im, int x, int y, float r, float g, float b) {
  if (x < 0 || y < 0 || x >= im.width || y >= im.height) return;
  float* p = im.at(x, y);
  p[0] = r; p[1] = g; p[2] = b;
}
void fillRect(Image& im, float x0, float y0, float x1, float y1, float r, float g, float b) {
  for (int y = (int)(y0 * im.height); y < (int)(y1 * im.height); ++y)
    for (int x = (int)(x0 * im.width); x < (int)(x1 * im.width); ++x) put(im, x, y, r, g, b);
}
void disc(Image& im, float cx, float cy, float rad, float r, float g, float b) {
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
float hashf(int x, int y) { return (fe_hash3((fe_uint)x, (fe_uint)y, 77u) & 0xffff) / 65535.f; }

Image makeScene(const std::string& name, int w, int h) {
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

void writePPM(const Image& im, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) { std::perror(path.c_str()); std::exit(1); }
  std::fprintf(f, "P6\n%d %d\n255\n", im.width, im.height);
  std::vector<unsigned char> row(im.width * 3);
  for (int y = 0; y < im.height; ++y) {
    for (int x = 0; x < im.width; ++x)
      for (int c = 0; c < 3; ++c) row[x * 3 + c] = (unsigned char)std::lround(std::clamp(im.at(x, y)[c], 0.f, 1.f) * 255.f);
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
}

bool setParam(FilmParams& p, const std::string& kv) {
  auto eq = kv.find('=');
  if (eq == std::string::npos) return false;
  std::string k = kv.substr(0, eq);
  float v = std::stof(kv.substr(eq + 1));
  bool b = v != 0.f;
  if (k == "mix") p.mix = v;
  else if (k == "bloom") p.bloom.enabled = b;
  else if (k == "bloom.intensity") p.bloom.intensity = v;
  else if (k == "bloom.threshold") p.bloom.threshold = v;
  else if (k == "bloom.knee") p.bloom.knee = v;
  else if (k == "bloom.radius") p.bloom.radius = v;
  else if (k == "halation") p.halation.enabled = b;
  else if (k == "halation.intensity") p.halation.intensity = v;
  else if (k == "halation.threshold") p.halation.threshold = v;
  else if (k == "halation.radius") p.halation.radius = v;
  else if (k == "halation.warmth") p.halation.warmth = v;
  else if (k == "grain") p.grain.enabled = b;
  else if (k == "grain.amount") p.grain.amount = v;
  else if (k == "grain.size") p.grain.size = v;
  else if (k == "grain.roughness") p.grain.roughness = v;
  else if (k == "grain.color") p.grain.color = b ? GrainColor::Color : GrainColor::Luma;
  else if (k == "quality") p.quality = b ? Quality::High : Quality::Normal;
  else return false;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string scene = "night-street", out = "out.ppm", before;
  int w = 1920, h = 1080, bench = 0;
  int64_t frame = 0;
  FilmParams p;
  p.bloom.enabled = p.halation.enabled = p.grain.enabled = true;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); } return argv[++i]; };
    if (a == "--scene") scene = next();
    else if (a == "--out") out = next();
    else if (a == "--before") before = next();
    else if (a == "--frame") frame = std::stoll(next());
    else if (a == "--bench") bench = std::stoi(next());
    else if (a == "--size") { std::string s = next(); std::sscanf(s.c_str(), "%dx%d", &w, &h); }
    else if (a == "--preset") {
      const Preset* pr = findPreset(next());
      if (!pr) { std::fprintf(stderr, "unknown preset\n"); return 2; }
      p = pr->params;
    } else if (a == "--set") {
      if (!setParam(p, next())) { std::fprintf(stderr, "bad --set\n"); return 2; }
    } else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
  }
  Image src = makeScene(scene, w, h);
  if (!before.empty()) writePPM(src, before);
  Image img = src;
  process(img, p, RenderContext{frame});
  writePPM(img, out);
  if (bench > 0) {
    std::vector<double> ms;
    for (int i = 0; i < bench; ++i) {
      Image t = src;
      auto t0 = std::chrono::steady_clock::now();
      process(t, p, RenderContext{frame + i});
      ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    std::printf("%s %dx%d runs=%d median=%.1fms p95=%.1fms\n", scene.c_str(), w, h, bench, ms[ms.size() / 2],
                ms[std::min(ms.size() - 1, (size_t)std::ceil(ms.size() * 0.95) - 1)]);
  }
  return 0;
}
