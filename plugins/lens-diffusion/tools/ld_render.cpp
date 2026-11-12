// ld_render — development-only CLI around the Lens & Diffusion core (not the plug-in).
//   ld_render --scene <name> [--size WxH] [--preset "<name>"] [--out out.ppm] [--before before.ppm]
// Scenes are the shared engine's synthetic scenes. Output: 8-bit PPM in the working encoding.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "lens.h"
#include "vv/scenes.h"

namespace {
void writePPM(const ld::Image& im, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fprintf(f, "P6\n%d %d\n255\n", im.width, im.height);
  for (int y = 0; y < im.height; ++y)
    for (int x = 0; x < im.width; ++x)
      for (int c = 0; c < 3; ++c) {
        const float v = std::clamp(im.at(x, y)[c], 0.f, 1.f);
        std::fputc(static_cast<int>(v * 255.f + 0.5f), f);
      }
  std::fclose(f);
}
}  // namespace

int main(int argc, char** argv) {
  std::string scene = "night-street", preset = "Vintage Lens", out = "out.ppm", before;
  int w = 1280, h = 720;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--scene") scene = next();
    else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &w, &h);
    else if (a == "--preset") preset = next();
    else if (a == "--out") out = next();
    else if (a == "--before") before = next();
    else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
  }
  const ld::Preset* p = ld::findPreset(preset);
  if (!p) { std::fprintf(stderr, "unknown preset %s\n", preset.c_str()); return 2; }
  ld::Image img = vv::scenes::makeScene(scene, w, h);
  if (img.empty()) { std::fprintf(stderr, "unknown scene %s\n", scene.c_str()); return 2; }
  if (!before.empty()) writePPM(img, before);
  ld::process(img, p->params);
  writePPM(img, out);
  return 0;
}
