// Runs the Denoise spatial GPU pass sequence (src/gpu/common) on the CPU, one "thread" per pixel
// like the CUDA kernels, and compares it with the CPU reference (dn_spatial.cpp): BGRA top-down
// 32f and 16f (Premiere) and RGBA bottom-up 32f (OpenFX).
#include <cmath>
#include <cstdio>
#include <vector>

#include "dn_gpu_pipeline.h"
#include "dn_synth.h"
#include "dn_testimages.h"
#include "gpu/vv_gpu_emulation.h"

using namespace dn;
using namespace dn::gpu;
using namespace dn::eval;

namespace {
struct Emu : vv::gpu::EmulationBackend {
  void dnPrepare(const void* src, bool half, Buf opp, Buf rgb, const FrameLayout& f) {
    grid(f.width, f.height, [&](uint32_t x, uint32_t y) {
      if (half) pxDnPrepare(static_cast<const H4*>(src), opp, rgb, f, x, y);
      else pxDnPrepare(static_cast<const F4*>(src), opp, rgb, f, x, y);
    });
  }
  void dnStarlet(Buf s, Buf t, Buf d, PlaneInfo p, int step) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxDnStarletH(s, t, p, step, x, y); });
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxDnStarletV(t, d, p, step, x, y); });
  }
  void dnBand(Buf c, Buf n, Buf b, Buf e, PlaneInfo p) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxDnBand(c, n, b, e, p, x, y); });
  }
  void dnVar(Buf low, Buf scale, Buf var, const DnProfileTable& t, const BandInfo& b) {
    grid(b.width, b.height, [&](uint32_t x, uint32_t y) { pxDnVar(low, scale, var, t, b, x, y); });
  }
  void dnPilot(Buf band, Buf E, Buf var, Buf p2, const BandInfo& b) {
    grid(b.width, b.height, [&](uint32_t x, uint32_t y) { pxDnPilot(band, E, var, p2, b, x, y); });
  }
  void dnAccumulate(Buf out, Buf band, Buf P, Buf var, const BandInfo& b) {
    grid(b.width, b.height, [&](uint32_t x, uint32_t y) { pxDnAccumulate(out, band, P, var, b, x, y); });
  }
  void dnFinish(const void* src, void* dst, bool half, Buf out, Buf res, const FrameLayout& f) {
    grid(f.width, f.height, [&](uint32_t x, uint32_t y) {
      if (half) pxDnFinish(static_cast<const H4*>(src), static_cast<H4*>(dst), out, res, f, x, y);
      else pxDnFinish(static_cast<const F4*>(src), static_cast<F4*>(dst), out, res, f, x, y);
    });
  }
};
}  // namespace

int main() {
  Emu backend;
  SpatialPipeline<Emu> pipe(backend);
  int failures = 0, cases = 0;
  double worst[3] = {0, 0, 0};
  for (const char* name : {"skin", "text", "weave", "chart"}) {
    for (auto [W, H] : std::vector<std::pair<int, int>>{{160, 90}, {97, 61}}) {
      const vv::Image src = makeTestImage(name, W, H);
      const Synth s = synthesize(src, NoiseSim(), 3u);
      const NoiseProfile prof = measureProfileStarlet(toRgbPlane(s.noisy), toRgbPlane(s.clean));
      SpatialParams sp;
      sp.detailRecovery = 0.1f;
      for (int mode = 0; mode < 3; ++mode) {  // 0 BGRA 32f, 1 BGRA 16f, 2 RGBA bottom-up 32f
        vv::Image in = s.noisy;
        const size_t n = size_t(W) * H;
        std::vector<F4> f32(n);
        std::vector<H4> f16(n);
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x) {
            float* p = in.at(x, y);
            if (mode == 2) f32[size_t(H - 1 - y) * W + x] = F4{p[0], p[1], p[2], p[3]};
            else if (mode == 1) {
              f16[size_t(y) * W + x] = H4{floatToHalf(p[2]), floatToHalf(p[1]), floatToHalf(p[0]), floatToHalf(p[3])};
              const F4 q = loadPx(f16.data(), uint32_t(y * W + x));  // reference sees the quantised input
              p[0] = q.z; p[1] = q.y; p[2] = q.x; p[3] = q.w;
            } else f32[size_t(y) * W + x] = F4{p[2], p[1], p[0], p[3]};
          }
        const vv::Image ref = denoiseSpatial(in, prof, sp);
        void* frame = mode == 1 ? static_cast<void*>(f16.data()) : static_cast<void*>(f32.data());
        if (!pipe.run(frame, frame, mode == 1, uint32_t(W), uint32_t(H), uint32_t(W), prof, sp, mode == 2)) ++failures;
        double d = 0;
        bool alpha = true;
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x) {
            const F4 q = mode == 2 ? f32[size_t(H - 1 - y) * W + x] : (mode == 1 ? loadPx(f16.data(), uint32_t(y * W + x)) : f32[size_t(y) * W + x]);
            const float g[4] = {mode == 2 ? q.x : q.z, q.y, mode == 2 ? q.z : q.x, q.w};
            const float* r = ref.at(x, y);
            for (int c = 0; c < 3; ++c) d = std::isfinite(g[c]) ? std::fmax(d, std::fabs(double(g[c]) - r[c])) : 1e9;
            alpha = alpha && g[3] == r[3];
          }
        // 32f: same maths, summation order of the blurs only. 16f: output quantisation. OpenFX:
        // bottom-up rows mirror the pyramid's [1 3 3 1] sampling grid at odd level heights (as in
        // Lens & Diffusion): an equally valid, mirrored grid of the local-energy blurs; measured up
        // to 2.3/255 on high-frequency weave at 97x61, exact at even sizes.
        const double tol = mode == 0 ? 1e-4 : (mode == 1 ? 4.0 / 255.0 : 3.0 / 255.0);
        const bool ok = alpha && d <= tol;
        if (!ok) std::printf("  FAIL %s %dx%d mode %d: max %.6f (x255 = %.3f) alpha %s\n", name, W, H, mode, d, d * 255, alpha ? "ok" : "CHANGED");
        failures += ok ? 0 : 1;
        worst[mode] = std::fmax(worst[mode], d);
        ++cases;
      }
    }
  }
  std::printf("Denoise spatial GPU emulation vs CPU: %d cases, worst 32f x255 = %.4f, 16f x255 = %.3f, OpenFX x255 = %.4f\n%s\n",
              cases, worst[0] * 255, worst[1] * 255, worst[2] * 255, failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
