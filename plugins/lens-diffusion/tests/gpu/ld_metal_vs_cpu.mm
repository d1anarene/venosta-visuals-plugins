// Runs the Lens & Diffusion Metal engine on a Mac GPU and compares it with the CPU reference on
// every synthetic scene and preset, for 32-bit and 16-bit float frames (BGRA, as from Premiere).
// Usage: ld_metal_vs_cpu <path/to/LensDiffusion.metallib>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "LensMetalEngine.h"
#include "lens.h"
#include "vv/scenes.h"

using namespace ld;

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: ld_metal_vs_cpu <LensDiffusion.metallib>\n"); return 2; }
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { std::printf("No Metal device - skipped\n"); return 0; }
    id<MTLCommandQueue> q = [dev newCommandQueue];
    LensMetalEngine eng(dev, argv[1]);
    if (!eng.ok()) { std::fprintf(stderr, "engine: %s\n", eng.error().c_str()); return 1; }
    int failures = 0, cases = 0;
    double worst[2] = {0, 0};
    for (const char* scene : vv::scenes::kAll) {
      const Image src = vv::scenes::makeScene(scene, 320, 180);
      for (const Preset& pr : builtinPresets()) {
        if (!pr.params.anyEffectActive()) continue;
        for (int half = 0; half < 2; ++half) {
          const size_t n = size_t(src.width) * src.height, bpp = half ? 8 : 16;
          id<MTLBuffer> buf = [dev newBufferWithLength:n * bpp options:MTLResourceStorageModeShared];
          Image ref = src;
          for (size_t i = 0; i < n; ++i) {
            float* s = &ref.px[i * 4];
            float bgra[4] = {s[2], s[1], s[0], s[3]};
            if (half) {
              __fp16* d = static_cast<__fp16*>(buf.contents) + i * 4;
              for (int c = 0; c < 4; ++c) d[c] = (__fp16)bgra[c];
              s[0] = d[2]; s[1] = d[1]; s[2] = d[0]; s[3] = d[3];  // reference sees the quantised input
            } else {
              std::memcpy(static_cast<float*>(buf.contents) + i * 4, bgra, 16);
            }
          }
          process(ref, pr.params);
          id<MTLCommandBuffer> cb = [q commandBuffer];
          if (!eng.encode(cb, buf, buf, half, uint32_t(src.width), uint32_t(src.height), uint32_t(src.width), pr.params, false)) {
            std::printf("  FAIL encode %s / %s\n", scene, pr.name.c_str());
            ++failures;
            continue;
          }
          [cb commit];
          [cb waitUntilCompleted];
          double maxAbs = 0;
          bool alphaOk = true;
          for (size_t i = 0; i < n; ++i) {
            float got[4];
            if (half) {
              const __fp16* d = static_cast<const __fp16*>(buf.contents) + i * 4;
              got[0] = d[2]; got[1] = d[1]; got[2] = d[0]; got[3] = d[3];
            } else {
              const float* d = static_cast<const float*>(buf.contents) + i * 4;
              got[0] = d[2]; got[1] = d[1]; got[2] = d[0]; got[3] = d[3];
            }
            for (int c = 0; c < 3; ++c) {
              const double e = std::fabs(double(got[c]) - double(ref.px[i * 4 + c]));
              maxAbs = std::isfinite(got[c]) ? std::fmax(maxAbs, e) : 1e9;
            }
            alphaOk = alphaOk && got[3] == ref.px[i * 4 + 3];
          }
          // 32f: GPU float maths (pow/exp in MSL) vs CPU; 16f: output quantisation.
          const double tol = half ? 4.0 / 255.0 : 2.0 / 255.0;  // same limits as Film Effects
          const bool ok = alphaOk && maxAbs <= tol;
          if (!ok) std::printf("  FAIL %s / %s / %s: max %.6f (x255 = %.3f) alpha %s\n", scene, pr.name.c_str(),
                               half ? "16f" : "32f", maxAbs, maxAbs * 255, alphaOk ? "ok" : "CHANGED");
          failures += ok ? 0 : 1;
          worst[half] = std::fmax(worst[half], maxAbs);
          ++cases;
        }
      }
    }
    std::printf("Lens Metal vs CPU: %d cases, worst 32f x255 = %.4f, 16f x255 = %.3f\n%s\n", cases, worst[0] * 255,
                worst[1] * 255, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
  }
}
