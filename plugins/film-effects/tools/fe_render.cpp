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
#ifndef _WIN32
#include <sys/resource.h>
#endif
#include "scenes.h"

using namespace fe;
using fe::scenes::makeScene;

namespace {

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
  bool mem = false;
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
    else if (a == "--mem") mem = true;
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
  auto peakKB = []() -> long {
#ifdef _WIN32
    return 0;  // --mem is not implemented on Windows
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return ru.ru_maxrss / 1024;  // bytes on macOS
#else
    return ru.ru_maxrss;  // KiB on Linux
#endif
#endif
  };
  long peakBefore = peakKB();
  process(img, p, RenderContext{frame});
  if (mem)
    std::printf("process() extra peak memory: %ld MB (frame %dx%d, image copy %zu MB)\n", (peakKB() - peakBefore) / 1024,
                w, h, img.px.size() * sizeof(float) >> 20);
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
