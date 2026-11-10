// Compares the Metal backend with the CPU reference on every synthetic scene and preset.
// macOS only. Build: see the "gpu" target in CMakeLists.txt (FE_BUILD_GPU_TESTS=ON).
// Usage: fe_metal_vs_cpu <path/to/FilmEffects.metallib>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MetalFilmEngine.h"
#include "pipeline/pipeline.h"
#include "scenes.h"

using namespace fe;

namespace {

struct Diff {
  double maxAbs = 0, meanAbs = 0;
  size_t n = 0, nonFinite = 0;
};

// Run the GPU engine on `src` (straight RGBA float) and return the result as RGBA float.
bool runGpu(id<MTLDevice> dev, id<MTLCommandQueue> q, MetalFilmEngine& eng, const Image& src, const FilmParams& p,
            int64_t key, bool half, Image& out, std::string& err) {
  const size_t px = size_t(src.width) * src.height;
  const size_t bpp = half ? 8 : 16;
  id<MTLBuffer> in = [dev newBufferWithLength:px * bpp options:MTLResourceStorageModeShared];
  id<MTLBuffer> dst = [dev newBufferWithLength:px * bpp options:MTLResourceStorageModeShared];
  for (size_t i = 0; i < px; ++i) {
    const float* s = &src.px[i * 4];
    float bgra[4] = {s[2], s[1], s[0], s[3]};
    if (half) {
      __fp16* d = static_cast<__fp16*>(in.contents) + i * 4;
      for (int c = 0; c < 4; ++c) d[c] = (__fp16)bgra[c];
    } else {
      std::memcpy(static_cast<float*>(in.contents) + i * 4, bgra, 16);
    }
  }
  MetalFrame fs, fd;
  fs.buffer = in;
  fd.buffer = dst;
  fs.width = fd.width = uint32_t(src.width);
  fs.height = fd.height = uint32_t(src.height);
  fs.rowPixels = fd.rowPixels = uint32_t(src.width);
  fs.halfFloat = fd.halfFloat = half;
  id<MTLCommandBuffer> cb = [q commandBuffer];
  if (!eng.encode(cb, fs, fd, p, key)) {
    err = "encode failed";
    return false;
  }
  [cb commit];
  [cb waitUntilCompleted];
  if (cb.status != MTLCommandBufferStatusCompleted) {
    err = cb.error ? cb.error.localizedDescription.UTF8String : "command buffer failed";
    return false;
  }
  out = Image(src.width, src.height);
  for (size_t i = 0; i < px; ++i) {
    float bgra[4];
    if (half) {
      const __fp16* d = static_cast<const __fp16*>(dst.contents) + i * 4;
      for (int c = 0; c < 4; ++c) bgra[c] = float(d[c]);
    } else {
      std::memcpy(bgra, static_cast<const float*>(dst.contents) + i * 4, 16);
    }
    out.px[i * 4] = bgra[2];
    out.px[i * 4 + 1] = bgra[1];
    out.px[i * 4 + 2] = bgra[0];
    out.px[i * 4 + 3] = bgra[3];
  }
  return true;
}

Diff compare(const Image& a, const Image& b) {
  Diff d;
  for (size_t i = 0; i < a.px.size(); ++i) {
    if ((i & 3) == 3) continue;  // alpha compared separately
    float x = a.px[i], y = b.px[i];
    if (!std::isfinite(y)) {
      ++d.nonFinite;
      continue;
    }
    // compare in display terms: clamp to 0..1 like an SDR output would
    double e = std::fabs(std::fmin(std::fmax(x, 0.f), 1.f) - std::fmin(std::fmax(y, 0.f), 1.f));
    d.maxAbs = std::fmax(d.maxAbs, e);
    d.meanAbs += e;
    ++d.n;
  }
  d.meanAbs /= std::max<size_t>(d.n, 1);
  return d;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc < 2) {
      std::fprintf(stderr, "usage: %s FilmEffects.metallib\n", argv[0]);
      return 2;
    }
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) {
      std::printf("SKIP: no Metal device available on this machine\n");
      return 0;
    }
    std::printf("Metal device: %s\n", dev.name.UTF8String);
    MetalFilmEngine eng(dev, argv[1]);
    if (!eng.ok()) {
      std::printf("FAIL: engine init: %s\n", eng.error().c_str());
      return 1;
    }
    id<MTLCommandQueue> q = [dev newCommandQueue];

    // Tolerances (display-referred, after clamping to 0..1). Float32 frames should agree closely;
    // half-float frames are limited by 16-bit storage of the input/output.
    const double kMax32 = 2.0 / 255.0, kMean32 = 0.1 / 255.0;
    const double kMax16 = 4.0 / 255.0, kMean16 = 0.5 / 255.0;
    int failures = 0;
    std::printf("%-14s %-20s %-4s %10s %10s %s\n", "scene", "preset", "fmt", "max*255", "mean*255", "result");
    for (const char* scene : scenes::kAll) {
      Image src = scenes::makeScene(scene, 960, 540);
      for (const Preset& pr : builtinPresets()) {
        if (!pr.params.anyEffectActive()) continue;
        Image cpu = src;
        process(cpu, pr.params, RenderContext{12});
        for (bool half : {false, true}) {
          Image gpu;
          std::string err;
          Image in = src;
          if (half)  // compare against a CPU run on the same half-quantised input
            for (float& v : in.px) v = float(__fp16(v));
          Image ref = in;
          process(ref, pr.params, RenderContext{12});
          if (!runGpu(dev, q, eng, in, pr.params, 12, half, gpu, err)) {
            std::printf("%-14s %-20s %-4s %s\n", scene, pr.name.c_str(), half ? "16f" : "32f", err.c_str());
            ++failures;
            continue;
          }
          Diff d = compare(ref, gpu);
          bool alphaOk = true;
          for (size_t i = 3; i < ref.px.size(); i += 4) alphaOk &= std::fabs(ref.px[i] - gpu.px[i]) < 1e-3f;
          bool ok = d.nonFinite == 0 && alphaOk && d.maxAbs <= (half ? kMax16 : kMax32) &&
                    d.meanAbs <= (half ? kMean16 : kMean32);
          failures += ok ? 0 : 1;
          std::printf("%-14s %-20s %-4s %10.3f %10.4f %s\n", scene, pr.name.c_str(), half ? "16f" : "32f",
                      d.maxAbs * 255, d.meanAbs * 255, ok ? "ok" : "FAIL");
        }
        (void)cpu;
      }
    }
    std::printf("\n%s: %d comparison(s) outside tolerance\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
  }
}
