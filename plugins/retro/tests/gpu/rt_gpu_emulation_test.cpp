// Runs the Retro GPU pass sequence (src/gpu/common) on the CPU, one "thread" per pixel exactly like
// the CUDA kernels, and compares it with the CPU reference on every synthetic scene, preset and a
// few frame keys: BGRA top-down 32f and 16f (Premiere) and RGBA bottom-up 32f (OpenFX).
#include <cmath>
#include <cstdio>
#include <vector>

#include "gpu/vv_gpu_emulation.h"
#include "rt.h"
#include "rt_gpu_pipeline.h"
#include "vv/scenes.h"

using namespace rt;
using namespace rt::gpu;

namespace {

struct Emu : vv::gpu::EmulationBackend {
  void rtPrepare(const void* src, bool half, Buf yuv, const FrameLayout& f) {
    grid(f.width, f.height, [&](uint32_t x, uint32_t y) {
      if (half) pxRtPrepare(static_cast<const H4*>(src), yuv, f, x, y);
      else pxRtPrepare(static_cast<const F4*>(src), yuv, f, x, y);
    });
  }
  void rtComposite(const void* src, void* dst, bool half, Buf l, Buf r, Buf c, const FrameLayout& f, const RtInfo& info) {
    // In place: the composite reads only its own source pixel, like on the GPU.
    grid(f.width, f.height, [&](uint32_t x, uint32_t y) {
      if (half) pxRtComposite(static_cast<const H4*>(src), static_cast<H4*>(dst), l, r, c, f, info, x, y);
      else pxRtComposite(static_cast<const F4*>(src), static_cast<F4*>(dst), l, r, c, f, info, x, y);
    });
  }
};

}  // namespace

int main() {
  Emu backend;
  Pipeline<Emu> pipe(backend);
  int failures = 0, cases = 0;
  double worst[3] = {0, 0, 0};
  const int64_t keys[] = {0, 1001, 987654321987LL};
  for (const char* scene : vv::scenes::kAll) {
    const Image src = vv::scenes::makeScene(scene, 320, 180);
    for (Preset pr : builtinPresets()) {
      if (!pr.params.anyActive()) continue;
      for (int64_t key : keys) {
        if (pr.name == "CRT Monitor") pr.params.tape = true;  // geometry of both kinds at once
        const size_t W = size_t(src.width), H = size_t(src.height), n = W * H;
        for (int mode = 0; mode < 3; ++mode) {  // 0: BGRA 32f, 1: BGRA 16f, 2: RGBA bottom-up 32f
          Image in = src;
          std::vector<F4> f32(n);
          std::vector<H4> f16(n);
          for (size_t y = 0; y < H; ++y)
            for (size_t x = 0; x < W; ++x) {
              float* p = in.at(int(x), int(y));
              if (mode == 2) {
                f32[(H - 1 - y) * W + x] = F4{p[0], p[1], p[2], p[3]};
              } else if (mode == 1) {
                f16[y * W + x] = H4{floatToHalf(p[2]), floatToHalf(p[1]), floatToHalf(p[0]), floatToHalf(p[3])};
                F4 q = loadPx(f16.data(), uint32_t(y * W + x));  // reference sees the quantised input
                p[0] = q.z; p[1] = q.y; p[2] = q.x; p[3] = q.w;
              } else {
                f32[y * W + x] = F4{p[2], p[1], p[0], p[3]};
              }
            }
          Image ref = in;
          process(ref, pr.params, key);
          void* frame = mode == 1 ? static_cast<void*>(f16.data()) : static_cast<void*>(f32.data());
          if (!pipe.run(frame, frame, mode == 1, uint32_t(W), uint32_t(H), uint32_t(W), pr.params, key, mode == 2, mode == 2))
            ++failures;
          double d = 0;
          bool alphaOk = true;
          for (size_t y = 0; y < H; ++y)
            for (size_t x = 0; x < W; ++x) {
              F4 q = mode == 2 ? f32[(H - 1 - y) * W + x] : (mode == 1 ? loadPx(f16.data(), uint32_t(y * W + x)) : f32[y * W + x]);
              const float g[4] = {mode == 2 ? q.x : q.z, q.y, mode == 2 ? q.z : q.x, q.w};
              const float* r = in.at(int(x), int(y));
              const float* e = ref.at(int(x), int(y));
              (void)r;
              for (int c = 0; c < 3; ++c) d = std::isfinite(g[c]) ? std::fmax(d, std::fabs(double(g[c]) - e[c])) : 1e9;
              alphaOk = alphaOk && g[3] == e[3];
            }
          // 32f: same maths (blur summation order only). 16f: output quantisation.
          const double tol = mode == 1 ? 4.0 / 255.0 : 1e-4;
          const bool ok = alphaOk && d <= tol;
          if (!ok)
            std::printf("  FAIL %s / %s / key %lld / mode %d: max %.6f (x255 = %.3f) alpha %s\n", scene, pr.name.c_str(),
                        (long long)key, mode, d, d * 255, alphaOk ? "ok" : "CHANGED");
          failures += ok ? 0 : 1;
          worst[mode] = std::fmax(worst[mode], d);
          ++cases;
        }
      }
    }
  }
  std::printf("Retro GPU pass emulation vs CPU reference: %d cases, worst 32f x255 = %.4f, 16f x255 = %.3f, OpenFX x255 = %.4f\n",
              cases, worst[0] * 255, worst[1] * 255, worst[2] * 255);
  std::printf("%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
