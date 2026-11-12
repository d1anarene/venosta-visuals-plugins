// Runs the Lens & Diffusion GPU pass sequence (src/gpu/common) on the CPU, one "thread" per pixel
// exactly like the CUDA kernels, and compares it with the CPU reference on every synthetic scene
// and preset: BGRA top-down 32f and 16f (Premiere) and RGBA bottom-up 32f (OpenFX).
#include <cmath>
#include <cstdio>
#include <vector>

#include "gpu/vv_gpu_emulation.h"
#include "ld_gpu_pipeline.h"
#include "lens.h"
#include "vv/scenes.h"

using namespace ld;
using namespace ld::gpu;

namespace {

struct Emu : vv::gpu::EmulationBackend {
  void ldPrepare(const void* src, bool half, Buf lin, const FrameInfo& fi) {
    grid(fi.width, fi.height, [&](uint32_t x, uint32_t y) {
      if (half) pxLdPrepare(static_cast<const H4*>(src), lin, fi, x, y);
      else pxLdPrepare(static_cast<const F4*>(src), lin, fi, x, y);
    });
  }
  void ldAberrate(Buf lin, Buf out, const AberrationInfo& a) {
    grid(a.width, a.height, [&](uint32_t x, uint32_t y) { pxLdAberrate(lin, out, a, x, y); });
  }
  void ldWeights(Buf lin, Buf s, Buf b, const WeightInfo& wi) {
    grid(wi.width, wi.height, [&](uint32_t x, uint32_t y) { pxLdWeights(lin, s, b, wi, x, y); });
  }
  void ldComposite(const void* src, void* dst, bool half, Buf lin, Buf s, Buf scat, Buf streak, const CompositeInfo& ci) {
    grid(ci.width, ci.height, [&](uint32_t x, uint32_t y) {
      if (half) pxLdComposite(static_cast<const H4*>(src), static_cast<H4*>(dst), lin, s, scat, streak, ci, x, y);
      else pxLdComposite(static_cast<const F4*>(src), static_cast<F4*>(dst), lin, s, scat, streak, ci, x, y);
    });
  }
};

double maxDiff(const Image& a, const Image& b, bool& alphaOk) {
  double m = 0;
  alphaOk = true;
  for (size_t i = 0; i < a.px.size(); ++i) {
    if (i % 4 == 3) { alphaOk = alphaOk && a.px[i] == b.px[i]; continue; }
    m = std::fmax(m, std::fabs(double(a.px[i]) - double(b.px[i])));
  }
  return m;
}

}  // namespace

int main() {
  Emu backend;
  Pipeline<Emu> pipe(backend);
  int failures = 0, cases = 0;
  double worst32 = 0, worst16 = 0, worstOfx = 0;
  for (const char* scene : vv::scenes::kAll) {
    const Image src = vv::scenes::makeScene(scene, 320, 180);
    for (const Preset& pr : builtinPresets()) {
      if (!pr.params.anyEffectActive()) continue;
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
              H4 hh{floatToHalf(p[2]), floatToHalf(p[1]), floatToHalf(p[0]), floatToHalf(p[3])};
              f16[y * W + x] = hh;
              F4 q = loadPx(f16.data(), uint32_t(y * W + x));  // reference sees the quantised input
              p[0] = q.z; p[1] = q.y; p[2] = q.x; p[3] = q.w;
            } else {
              f32[y * W + x] = F4{p[2], p[1], p[0], p[3]};
            }
          }
        Image ref = in;
        process(ref, pr.params);
        void* frame = mode == 1 ? static_cast<void*>(f16.data()) : static_cast<void*>(f32.data());
        if (!pipe.run(frame, frame, mode == 1, uint32_t(W), uint32_t(H), uint32_t(W), pr.params, mode == 2)) ++failures;
        Image got(in.width, in.height);
        for (size_t y = 0; y < H; ++y)
          for (size_t x = 0; x < W; ++x) {
            float* g = got.at(int(x), int(y));
            if (mode == 2) {
              F4 q = f32[(H - 1 - y) * W + x];
              g[0] = q.x; g[1] = q.y; g[2] = q.z; g[3] = q.w;
            } else {
              F4 q = mode == 1 ? loadPx(f16.data(), uint32_t(y * W + x)) : f32[y * W + x];
              g[0] = q.z; g[1] = q.y; g[2] = q.x; g[3] = q.w;
            }
          }
        bool alphaOk;
        const double d = maxDiff(ref, got, alphaOk);
        // 32f: same maths, summation order only. 16f: output quantisation. OpenFX layout: the
        // pyramid's sampling grid is mirrored for bottom-up rows, small blur differences allowed.
        const double tol = mode == 0 ? 1e-4 : (mode == 1 ? 4.0 / 255.0 : 2.0 / 255.0);
        const bool ok = alphaOk && d <= tol;
        if (!ok) std::printf("  FAIL %s / %s / mode %d: max %.6f (x255 = %.3f) alpha %s\n", scene, pr.name.c_str(), mode, d, d * 255, alphaOk ? "ok" : "CHANGED");
        failures += ok ? 0 : 1;
        double& worst = mode == 0 ? worst32 : (mode == 1 ? worst16 : worstOfx);
        worst = std::fmax(worst, d);
        ++cases;
      }
    }
  }
  std::printf("Lens GPU pass emulation vs CPU reference: %d cases, worst 32f x255 = %.4f, 16f x255 = %.3f, OpenFX x255 = %.4f\n",
              cases, worst32 * 255, worst16 * 255, worstOfx * 255);
  std::printf("%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
