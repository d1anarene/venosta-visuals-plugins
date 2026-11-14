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
  static const void* ptr(Buf b) { return b; }
  static void* mptr(Buf b) { return b; }
  void dnHalve(const void* src, bool half, Buf d, const FrameLayout& f, PlaneInfo hp) {
    grid(hp.width, hp.height, [&](uint32_t x, uint32_t y) {
      if (half) pxDnHalve(static_cast<const H4*>(src), d, f, hp, x, y);
      else pxDnHalve(static_cast<const F4*>(src), d, f, hp, x, y);
    });
  }
  void dnUpFinish(const void* src, void* dst, bool half, Buf low, const FrameLayout& f, PlaneInfo hp) {
    grid(f.width, f.height, [&](uint32_t x, uint32_t y) {
      if (half) pxDnUpFinish(static_cast<const H4*>(src), static_cast<H4*>(dst), low, f, hp, x, y);
      else pxDnUpFinish(static_cast<const F4*>(src), static_cast<F4*>(dst), low, f, hp, x, y);
    });
  }
  void dnPrepare(const void* src, bool half, Buf opp, const FrameLayout& f) {
    grid(f.width, f.height, [&](uint32_t x, uint32_t y) {
      if (half) pxDnPrepare(static_cast<const H4*>(src), opp, f, x, y);
      else pxDnPrepare(static_cast<const F4*>(src), opp, f, x, y);
    });
  }
  void dnCrop(const void* src, bool half, Buf rgb, const CropInfo& c) {
    grid(c.width, c.height, [&](uint32_t x, uint32_t y) {
      if (half) pxDnCrop(static_cast<const H4*>(src), rgb, c, x, y);
      else pxDnCrop(static_cast<const F4*>(src), rgb, c, x, y);
    });
  }
  void dnStarletV(Buf s, Buf d, PlaneInfo p, int step) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxDnStarletV(s, d, p, step, x, y); });
  }
  void dnBlocks(Buf c, Buf n, Buf b, size_t off, const BlockGrid& g) {
    grid(g.cols, g.rows, [&](uint32_t q, uint32_t r) { pxDnBlock(c, n, b + off, g, q, r); });
  }
  void dnStarletH(Buf s, Buf t, PlaneInfo p, int step) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxDnStarletH(s, t, p, step, x, y); });
  }
  void dnStarletVBand(Buf t, Buf c, Buf n, Buf b, Buf e, Buf v, const DnProfileTable& tab, const BandInfo& bi, int step) {
    grid(bi.width, bi.height, [&](uint32_t x, uint32_t y) { pxDnStarletVBand(t, c, n, b, e, v, tab, bi, step, x, y); });
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
  // Noise estimate: device part (crop, starlet, block statistics) + estimateFromBlocks vs the CPU
  // estimate on the same crop. 32f: float block sums vs double on the CPU.
  double worstEst = 0;
  for (const char* name : {"skin", "weave"}) {
    const int W = 211, H = 133;
    const vv::Image src = makeTestImage(name, W, H);
    const Synth s = synthesize(src, NoiseSim(), 5u);
    for (int mode = 0; mode < 3; ++mode) {
      const size_t n = size_t(W) * H;
      std::vector<F4> f32(n);
      std::vector<H4> f16(n);
      vv::Image in = s.noisy;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          float* p = in.at(x, y);
          if (mode == 2) f32[size_t(H - 1 - y) * W + x] = F4{p[0], p[1], p[2], p[3]};
          else if (mode == 1) {
            f16[size_t(y) * W + x] = H4{floatToHalf(p[2]), floatToHalf(p[1]), floatToHalf(p[0]), floatToHalf(p[3])};
            const F4 q = loadPx(f16.data(), uint32_t(y * W + x));
            p[0] = q.z; p[1] = q.y; p[2] = q.x; p[3] = q.w;
          } else f32[size_t(y) * W + x] = F4{p[2], p[1], p[0], p[3]};
        }
      // CPU reference crop: centred, top row first.
      const int cw = 150, ch = 100, x0 = (W - cw) / 2, y0 = (H - ch) / 2;
      vv::Image crop(cw, ch);
      for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
          for (int c = 0; c < 4; ++c) crop.at(x, y)[c] = in.at(x0 + x, y0 + y)[c];
      const NoiseProfile ref = estimateProfile(toRgbPlane(crop), nullptr, Domain::Starlet);
      void* frame = mode == 1 ? static_cast<void*>(f16.data()) : static_cast<void*>(f32.data());
      if (!pipe.encodeEstimate(frame, mode == 1, uint32_t(W), uint32_t(H), uint32_t(W), mode == 2, mode == 2, cw, ch)) {
        ++failures;
        continue;
      }
      const NoiseProfile got =
          estimateFromBlocks(unpackBlocks(pipe.blocks(), pipe.blockCount(), DN_BANDS), nullptr, Domain::Starlet);
      double d = 0;
      for (int b = 0; b < kBands; ++b)
        for (int c = 0; c < 3; ++c) {
          for (int k = 0; k < kBins; ++k)
            d = std::fmax(d, std::fabs(double(got.sigma[b][c][k]) - ref.sigma[b][c][k]) /
                                 std::fmax(1e-6, double(ref.sigma[b][c][k])));
          d = std::fmax(d, std::fabs(double(got.corr[b][c]) - ref.corr[b][c]));
        }
      const bool ok = d < 1e-3;
      if (!ok) std::printf("  FAIL estimate %s mode %d: max relative difference %.6f\n", name, mode, d);
      failures += ok ? 0 : 1;
      worstEst = std::fmax(worstEst, d);
      ++cases;
    }
  }
  // Quality Preview: half resolution (2x2 average, filter, bilinear upsample) and the estimate on
  // the half-size frame, against the CPU reference (halveImage / denoiseSpatial / upsampleImage).
  double worstPrev = 0;
  for (const char* name : {"skin", "text"}) {
    for (auto [W, H] : std::vector<std::pair<int, int>>{{161, 91}, {120, 68}}) {
      const Synth s = synthesize(makeTestImage(name, W, H), NoiseSim(), 7u);
      for (int mode = 0; mode < 3; ++mode) {
        const size_t n = size_t(W) * H;
        std::vector<F4> f32(n);
        std::vector<H4> f16(n);
        vv::Image in = s.noisy;
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x) {
            float* p = in.at(x, y);
            if (mode == 2) f32[size_t(H - 1 - y) * W + x] = F4{p[0], p[1], p[2], p[3]};
            else if (mode == 1) {
              f16[size_t(y) * W + x] = H4{floatToHalf(p[2]), floatToHalf(p[1]), floatToHalf(p[0]), floatToHalf(p[3])};
              const F4 q = loadPx(f16.data(), uint32_t(y * W + x));
              p[0] = q.z; p[1] = q.y; p[2] = q.x; p[3] = q.w;
            } else f32[size_t(y) * W + x] = F4{p[2], p[1], p[0], p[3]};
          }
        const vv::Image small = halveImage(in);
        const NoiseProfile wantProf = estimateProfile(toRgbPlane(small), nullptr, Domain::Starlet);
        SpatialParams sp;
        const vv::Image ref = upsampleImage(denoiseSpatial(small, wantProf, sp), W, H);
        void* frame = mode == 1 ? static_cast<void*>(f16.data()) : static_cast<void*>(f32.data());
        double de = 1e9;
        if (pipe.encodeEstimate(frame, mode == 1, uint32_t(W), uint32_t(H), uint32_t(W), mode == 2, mode == 2, 4096, 4096,
                                true)) {
          const NoiseProfile got =
              estimateFromBlocks(unpackBlocks(pipe.blocks(), pipe.blockCount(), DN_BANDS), nullptr, Domain::Starlet);
          de = 0;
          for (int b = 0; b < kBands; ++b)
            for (int c = 0; c < 3; ++c)
              for (int k = 0; k < kBins; ++k)
                de = std::fmax(de, std::fabs(double(got.sigma[b][c][k]) - wantProf.sigma[b][c][k]) /
                                       std::fmax(1e-6, double(wantProf.sigma[b][c][k])));
        }
        if (!pipe.run(frame, frame, mode == 1, uint32_t(W), uint32_t(H), uint32_t(W), wantProf, sp, mode == 2, 0, 1.0f, true))
          ++failures;
        double d = 0, errGpu = 0, errRef = 0;
        bool alpha = true;
        for (int y = 0; y < H; ++y)
          for (int x = 0; x < W; ++x) {
            const F4 q = mode == 2 ? f32[size_t(H - 1 - y) * W + x] : (mode == 1 ? loadPx(f16.data(), uint32_t(y * W + x)) : f32[size_t(y) * W + x]);
            const float g[4] = {mode == 2 ? q.x : q.z, q.y, mode == 2 ? q.z : q.x, q.w};
            const float* r = ref.at(x, y);
            const float* c0 = s.clean.at(x, y);
            for (int c = 0; c < 3; ++c) {
              d = std::isfinite(g[c]) ? std::fmax(d, std::fabs(double(g[c]) - r[c])) : 1e9;
              errGpu += double(g[c] - c0[c]) * (g[c] - c0[c]);
              errRef += double(r[c] - c0[c]) * (r[c] - c0[c]);
            }
            alpha = alpha && g[3] == in.at(x, y)[3];
          }
        // 32f top-down: same maths. 16f: output quantisation. Bottom-up (OpenFX layout): the
        // pyramid's mirrored sampling grid as in the full-size test (3/255) at even heights; at odd
        // heights the 2x2 grid itself is mirrored (an equally valid preview), so only its error
        // against the clean image is compared (within 10 % of the reference's).
        const bool exact = mode != 2 || H % 2 == 0;
        const double tol = mode == 0 ? 1e-4 : (mode == 1 ? 4.0 / 255.0 : 3.0 / 255.0);
        const bool ok = alpha && (exact ? d <= tol && de < 1e-3 : errGpu <= errRef * 1.1);
        if (!ok) std::printf("  FAIL preview %s %dx%d mode %d: max %.6f estimate %.2e alpha %s\n", name, W, H, mode, d, de, alpha ? "ok" : "CHANGED");
        failures += ok ? 0 : 1;
        if (mode == 0) worstPrev = std::fmax(worstPrev, d);
        ++cases;
      }
    }
  }
  std::printf("Quality Preview (half resolution) vs CPU: worst 32f x255 = %.4f\n", worstPrev * 255);
  std::printf("Noise estimate on the device vs CPU: worst relative sigma / corr difference %.2e\n", worstEst);
  std::printf("Denoise spatial GPU emulation vs CPU: %d cases, worst 32f x255 = %.4f, 16f x255 = %.3f, OpenFX x255 = %.4f\n%s\n",
              cases, worst[0] * 255, worst[1] * 255, worst[2] * 255, failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
