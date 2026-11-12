// Runs the shared GPU pass sequence (src/gpu/common) on the CPU, one "thread" per pixel exactly as
// the CUDA kernels do, and compares it with the CPU reference pipeline on every synthetic scene and
// preset, for 32-bit and 16-bit float frames. This verifies the CUDA backend's maths and pass
// orchestration without an NVIDIA GPU; the CUDA kernels are thin wrappers around the same
// per-pixel functions (src/gpu/cuda/CudaFilmEngine.cu).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

#include "gpu/common/fe_gpu_pipeline.h"
#include "pipeline/pipeline.h"
#include "scenes.h"

using namespace fe;
using namespace fe::gpu;

namespace {

struct EmuBackend {
  using Buf = F4*;
  std::deque<std::vector<F4>> store;
  void releaseAll() { store.clear(); }
  Buf alloc(size_t n) {
    store.emplace_back(n, F4{NAN, NAN, NAN, NAN});  // poison: reading unwritten memory shows up
    return store.back().data();
  }
  template <typename Fn>
  static void grid(uint32_t w, uint32_t h, Fn fn) {
    for (uint32_t y = 0; y < h; ++y)
      for (uint32_t x = 0; x < w; ++x) fn(x, y);
  }
  void prepare(const void* frame, bool half, Buf lin, Buf bright, Buf halSrc, const FrameInfo& fi) {
    grid(fi.width, fi.height, [&](uint32_t x, uint32_t y) {
      if (half) pxPrepare(static_cast<const H4*>(frame), lin, bright, halSrc, fi, x, y);
      else pxPrepare(static_cast<const F4*>(frame), lin, bright, halSrc, fi, x, y);
    });
  }
  void downsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    grid(di.width, di.height, [&](uint32_t x, uint32_t y) { pxDownsample(s, d, si, di, x, y); });
  }
  void blurH(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxBlurH(s, d, p, k.data(), int(k.size() / 2), x, y); });
  }
  void blurV(Buf s, Buf d, PlaneInfo p, const std::vector<float>& k) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxBlurV(s, d, p, k.data(), int(k.size() / 2), x, y); });
  }
  void upsample(Buf s, Buf d, PlaneInfo si, PlaneInfo di) {
    grid(di.width, di.height, [&](uint32_t x, uint32_t y) { pxUpsample(s, d, si, di, x, y); });
  }
  void axpy(Buf acc, Buf v, float w, PlaneInfo p) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxAxpy(acc, v, w, p, x, y); });
  }
  void clear(Buf d, PlaneInfo p) {
    grid(p.width, p.height, [&](uint32_t x, uint32_t y) { pxClear(d, p, x, y); });
  }
  void composite(const void* src, void* dst, bool half, Buf lin, Buf bloom, Buf hn, Buf hf, Buf hs,
                 const CompositeInfo& ci) {
    grid(ci.width, ci.height, [&](uint32_t x, uint32_t y) {
      if (half) pxComposite(static_cast<const H4*>(src), static_cast<H4*>(dst), lin, bloom, hn, hf, hs, ci, x, y);
      else pxComposite(static_cast<const F4*>(src), static_cast<F4*>(dst), lin, bloom, hn, hf, hs, ci, x, y);
    });
  }
  void compositeFilm(const void* src, bool half, Buf film, Buf lin, Buf bloom, Buf hn, Buf hf, Buf hs,
                     const CompositeInfo& ci) {
    grid(ci.width, ci.height, [&](uint32_t x, uint32_t y) {
      if (half) pxCompositeFilm(static_cast<const H4*>(src), film, lin, bloom, hn, hf, hs, ci, x, y);
      else pxCompositeFilm(static_cast<const F4*>(src), film, lin, bloom, hn, hf, hs, ci, x, y);
    });
  }
  void gate(const void* src, void* dst, bool half, Buf film, const GateInfo& g) {
    // Write to a separate buffer first: src and dst may be the same (in place), and the gate reads
    // only `film` and its own source pixel, exactly like the GPU kernel.
    grid(g.width, g.height, [&](uint32_t x, uint32_t y) {
      if (half) pxGate(static_cast<const H4*>(src), static_cast<H4*>(dst), film, g, x, y);
      else pxGate(static_cast<const F4*>(src), static_cast<F4*>(dst), film, g, x, y);
    });
  }
};

int failures = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) {                                                          \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

void testHalfConversion() {
#if defined(__FLT16_MAX__) && !defined(_MSC_VER)
  int bad = 0;
  for (uint32_t h = 0; h < 65536; ++h) {
    _Float16 ref;
    uint16_t hb = uint16_t(h);
    std::memcpy(&ref, &hb, 2);
    float a = halfToFloat(hb), b = float(ref);
    if (!(a == b || (std::isnan(a) && std::isnan(b)))) ++bad;
  }
  for (uint32_t i = 0; i < 2000000; ++i) {
    uint32_t bits = vv_hash(i) ;
    float f;
    std::memcpy(&f, &bits, 4);
    if (std::isnan(f)) continue;
    _Float16 r = static_cast<_Float16>(f);
    uint16_t rb;
    std::memcpy(&rb, &r, 2);
    if (floatToHalf(f) != rb) ++bad;
  }
  std::printf("half conversion vs compiler _Float16: %d mismatches\n", bad);
  CHECK(bad == 0);
#else
  // Round-trip sanity when no native half type is available (e.g. MSVC).
  for (float f : {0.0f, 1.0f, -2.5f, 0.333f, 65504.0f, 6.1e-5f}) CHECK(std::fabs(halfToFloat(floatToHalf(f)) - f) <= std::fabs(f) * 1e-3f);
  std::printf("half conversion: round-trip checks only (no native _Float16)\n");
#endif
}

double compare(const Image& ref, const Image& got, bool& alphaOk) {
  double maxAbs = 0;
  alphaOk = true;
  for (size_t i = 0; i < ref.px.size(); ++i) {
    if ((i & 3) == 3) {
      alphaOk &= ref.px[i] == got.px[i];
      continue;
    }
    if (!std::isfinite(got.px[i])) return 1e9;
    double a = std::fmin(std::fmax(ref.px[i], 0.f), 1.f), b = std::fmin(std::fmax(got.px[i], 0.f), 1.f);
    maxAbs = std::fmax(maxAbs, std::fabs(a - b));
  }
  return maxAbs;
}

}  // namespace

int main() {
  testHalfConversion();
  EmuBackend backend;
  Pipeline<EmuBackend> pipe(backend);
  const double kMax32 = 1e-4, kMax16 = 4.0 / 255.0;  // 32f: same maths, only summation order
  double worst32 = 0, worst16 = 0, worstOfx = 0;
  int cases = 0;
  for (const char* scene : scenes::kAll) {
    const Image src = scenes::makeScene(scene, 320, 180);
    for (const Preset& pr : builtinPresets()) {
      if (!pr.params.anyEffectActive()) continue;
      for (bool half : {false, true}) {
        Image in = src;
        const size_t n = size_t(in.width) * in.height;
        std::vector<F4> f32(n);
        std::vector<H4> f16(n);
        for (size_t i = 0; i < n; ++i) {
          const float* p = &in.px[i * 4];
          F4 bgra{p[2], p[1], p[0], p[3]};
          if (half) {
            f16[i] = H4{floatToHalf(bgra.x), floatToHalf(bgra.y), floatToHalf(bgra.z), floatToHalf(bgra.w)};
            F4 q = loadPx(f16.data(), uint32_t(i));  // the CPU reference sees the same quantised input
            in.px[i * 4] = q.z; in.px[i * 4 + 1] = q.y; in.px[i * 4 + 2] = q.x; in.px[i * 4 + 3] = q.w;
          } else {
            f32[i] = bgra;
          }
        }
        Image ref = in;
        process(ref, pr.params, RenderContext{12});
        // in place, like Premiere's GPU Render may do
        void* frame = half ? static_cast<void*>(f16.data()) : static_cast<void*>(f32.data());
        CHECK(pipe.run(frame, frame, half, uint32_t(in.width), uint32_t(in.height), uint32_t(in.width), pr.params, 12));
        Image got(in.width, in.height);
        for (size_t i = 0; i < n; ++i) {
          F4 q = half ? loadPx(f16.data(), uint32_t(i)) : f32[i];
          got.px[i * 4] = q.z; got.px[i * 4 + 1] = q.y; got.px[i * 4 + 2] = q.x; got.px[i * 4 + 3] = q.w;
        }
        bool alphaOk;
        double d = compare(ref, got, alphaOk);
        bool ok = alphaOk && d <= (half ? kMax16 : kMax32);
        if (!ok) std::printf("  FAIL %s / %s / %s: max %.6f (x255 = %.3f) alpha %s\n", scene, pr.name.c_str(),
                             half ? "16f" : "32f", d, d * 255, alphaOk ? "ok" : "CHANGED");
        failures += ok ? 0 : 1;
        (half ? worst16 : worst32) = std::fmax(half ? worst16 : worst32, d);
        ++cases;
      }
    }
  }
  // OpenFX layout: RGBA, bottom row first. Must give the same picture (grain included) as the
  // CPU reference on the top-down image.
  for (const char* scene : scenes::kAll) {
    const Image in = scenes::makeScene(scene, 320, 180);
    for (const Preset& pr : builtinPresets()) {
      if (!pr.params.anyEffectActive()) continue;
      const size_t W = size_t(in.width), H = size_t(in.height);
      std::vector<F4> f(W * H);
      for (size_t y = 0; y < H; ++y)
        for (size_t x = 0; x < W; ++x) {
          const float* p = in.at(int(x), int(y));
          f[(H - 1 - y) * W + x] = F4{p[0], p[1], p[2], p[3]};
        }
      Image ref = in;
      process(ref, pr.params, RenderContext{12});
      CHECK(pipe.run(f.data(), f.data(), false, uint32_t(W), uint32_t(H), uint32_t(W), pr.params, 12, true, true));
      Image got(in.width, in.height);
      for (size_t y = 0; y < H; ++y)
        for (size_t x = 0; x < W; ++x) {
          const F4 q = f[(H - 1 - y) * W + x];
          float* g = got.at(int(x), int(y));
          g[0] = q.x; g[1] = q.y; g[2] = q.z; g[3] = q.w;
        }
      bool alphaOk;
      const double d = compare(ref, got, alphaOk);
      // Bottom-up rows mirror the bloom pyramid's sampling grid, so allow a small blur difference.
      const bool ok = alphaOk && d <= 2.0 / 255.0;
      if (!ok) std::printf("  FAIL OFX layout %s / %s: max %.6f (x255 = %.3f)\n", scene, pr.name.c_str(), d, d * 255);
      failures += ok ? 0 : 1;
      worstOfx = std::fmax(worstOfx, d);
      ++cases;
    }
  }
  std::printf("OpenFX layout (RGBA, bottom-up) vs CPU reference: worst x255 = %.4f\n", worstOfx * 255);
  std::printf("GPU pass emulation vs CPU reference: %d cases, worst 32f %.2e (x255 = %.4f), worst 16f x255 = %.3f\n",
              cases, worst32, worst32 * 255, worst16 * 255);
  std::printf("%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
