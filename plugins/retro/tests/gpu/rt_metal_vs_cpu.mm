// Runs the Retro Metal engine on a Mac GPU and compares it with the CPU reference
// on every synthetic scene and preset (32f and 16f, BGRA top-down; RGBA bottom-up 32f).
// Usage: rt_metal_vs_cpu <path/to/Retro.metallib>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "RetroMetalEngine.h"
#include "rt.h"
#include "vv/scenes.h"

using namespace rt;

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: rt_metal_vs_cpu <metallib>\n"); return 2; }
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { std::printf("No Metal device - skipped\n"); return 0; }
    id<MTLCommandQueue> q = [dev newCommandQueue];
    RetroMetalEngine eng(dev, argv[1]);
    if (!eng.ok()) { std::fprintf(stderr, "engine: %s\n", eng.error().c_str()); return 1; }
    int failures = 0, cases = 0;
    double worst[3] = {0, 0, 0};
    for (const char* scene : vv::scenes::kAll) {
      const Image src = vv::scenes::makeScene(scene, 320, 180);
      for (Preset pr : builtinPresets()) {
        if (!pr.params.anyActive()) continue;
        if (pr.name == "CRT Monitor") pr.params.tape = true;
        const int64_t key = 1001;
        for (int mode = 0; mode < 3; ++mode) {  // 0: BGRA 32f, 1: BGRA 16f, 2: RGBA bottom-up 32f
          const bool half = mode == 1, ofx = mode == 2;
          const size_t W = size_t(src.width), H = size_t(src.height), n = W * H, bpp = half ? 8 : 16;
          id<MTLBuffer> buf = [dev newBufferWithLength:n * bpp options:MTLResourceStorageModeShared];
          Image ref = src;
          for (size_t y = 0; y < H; ++y)
            for (size_t x = 0; x < W; ++x) {
              float* s = ref.at(int(x), int(y));
              const size_t i = (ofx ? H - 1 - y : y) * W + x;
              float v[4] = {ofx ? s[0] : s[2], s[1], ofx ? s[2] : s[0], s[3]};
              if (half) {
                __fp16* d = static_cast<__fp16*>(buf.contents) + i * 4;
                for (int c = 0; c < 4; ++c) d[c] = (__fp16)v[c];
                s[0] = d[2]; s[1] = d[1]; s[2] = d[0]; s[3] = d[3];
              } else {
                std::memcpy(static_cast<float*>(buf.contents) + i * 4, v, 16);
              }
            }
          process(ref, pr.params, key);
          id<MTLCommandBuffer> cb = [q commandBuffer];
          eng.encode(cb, buf, buf, half, uint32_t(W), uint32_t(H), uint32_t(W), pr.params, key, ofx, ofx);
          [cb commit];
          [cb waitUntilCompleted];
          double maxAbs = 0;
          size_t off = 0;  // pixels outside tolerance (boundary rounding)
          bool alphaOk = true;
          const double tol = half ? 4.0 / 255.0 : 1e-3;
          for (size_t y = 0; y < H; ++y)
            for (size_t x = 0; x < W; ++x) {
              const size_t i = (ofx ? H - 1 - y : y) * W + x;
              float g[4];
              if (half) {
                const __fp16* d = static_cast<const __fp16*>(buf.contents) + i * 4;
                g[0] = d[2]; g[1] = d[1]; g[2] = d[0]; g[3] = d[3];
              } else {
                const float* d = static_cast<const float*>(buf.contents) + i * 4;
                g[0] = ofx ? d[0] : d[2]; g[1] = d[1]; g[2] = ofx ? d[2] : d[0]; g[3] = d[3];
              }
              const float* r = ref.at(int(x), int(y));
              double d = 0;
              for (int c = 0; c < 3; ++c) d = std::isfinite(g[c]) ? std::fmax(d, std::fabs(g[c] - r[c])) : 1e9;
              maxAbs = std::fmax(maxAbs, d);
              off += d > tol;
              alphaOk = alphaOk && g[3] == r[3];
            }
          // Same maths as the CPU. GPU sin/floor/blur rounding can move a sample across a pixel or
          // a hash cell boundary (a dropout or tracking-noise column): allow at most 0.1 % of the
          // pixels, and report them.
          const bool ok = alphaOk && off * 1000 <= n;
          if (off) std::printf("  %s / %s / mode %d: %zu boundary pixel(s)\n", scene, pr.name.c_str(), mode, off);
          if (!ok) std::printf("  FAIL %s / %s / mode %d: max %.6f, %zu pixels off, alpha %s\n", scene, pr.name.c_str(), mode, maxAbs, off, alphaOk ? "ok" : "CHANGED");
          failures += ok ? 0 : 1;
          worst[mode] = std::fmax(worst[mode], maxAbs);
          ++cases;
        }
      }
    }
    std::printf("Retro Metal vs CPU: %d cases, worst 32f %.2e, 16f x255 = %.3f, OpenFX %.2e\n%s\n",
                cases, worst[0], worst[1] * 255, worst[2], failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
  }
}
