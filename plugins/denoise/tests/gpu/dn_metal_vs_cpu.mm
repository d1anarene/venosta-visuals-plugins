// Runs the Denoise spatial filter on a Mac GPU (Metal) and compares it with the CPU reference
// (dn_spatial.cpp): BGRA top-down 32f and 16f, RGBA bottom-up 32f (OpenFX). Also the device part
// of the noise estimate (crop, starlet, block statistics) against the CPU estimate.
// Usage: dn_metal_vs_cpu <path/to/Denoise.metallib>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "DnMetalEngine.h"
#include "dn_synth.h"
#include "dn_testimages.h"

using namespace dn;
using namespace dn::eval;

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: dn_metal_vs_cpu <metallib>\n"); return 2; }
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { std::printf("No Metal device - skipped\n"); return 0; }
    id<MTLCommandQueue> q = [dev newCommandQueue];
    DnMetalEngine eng(dev, argv[1]);
    if (!eng.ok()) { std::fprintf(stderr, "engine: %s\n", eng.error().c_str()); return 1; }
    int failures = 0, cases = 0;
    double worst[3] = {0, 0, 0}, worstEst = 0;
    for (const char* name : {"skin", "text", "weave", "chart"}) {
      for (auto [W, H] : std::vector<std::pair<int, int>>{{320, 180}, {97, 61}}) {
        const Synth s = synthesize(makeTestImage(name, W, H), NoiseSim(), 3u);
        const NoiseProfile prof = measureProfileStarlet(toRgbPlane(s.noisy), toRgbPlane(s.clean));
        SpatialParams sp;
        sp.detailRecovery = 0.1f;
        for (int mode = 0; mode < 3; ++mode) {
          const bool half = mode == 1, ofx = mode == 2;
          const size_t n = size_t(W) * H, bpp = half ? 8 : 16;
          id<MTLBuffer> buf = [dev newBufferWithLength:n * bpp options:MTLResourceStorageModeShared];
          vv::Image in = s.noisy;
          for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
              float* p = in.at(x, y);
              const size_t i = size_t(ofx ? H - 1 - y : y) * W + x;
              float v[4] = {ofx ? p[0] : p[2], p[1], ofx ? p[2] : p[0], p[3]};
              if (half) {
                __fp16* d = static_cast<__fp16*>(buf.contents) + i * 4;
                for (int c = 0; c < 4; ++c) d[c] = (__fp16)v[c];
                p[0] = d[2]; p[1] = d[1]; p[2] = d[0]; p[3] = d[3];
              } else {
                std::memcpy(static_cast<float*>(buf.contents) + i * 4, v, 16);
              }
            }
          const vv::Image ref = denoiseSpatial(in, prof, sp);
          {  // noise estimate on a centred crop, before the frame is overwritten
            const int cw = W < 200 ? W : 200, ch = H < 120 ? H : 120, x0 = (W - cw) / 2, y0 = (H - ch) / 2;
            vv::Image crop(cw, ch);
            for (int y = 0; y < ch; ++y)
              for (int x = 0; x < cw; ++x)
                for (int c = 0; c < 4; ++c) crop.at(x, y)[c] = in.at(x0 + x, y0 + y)[c];
            const NoiseProfile want = estimateProfile(toRgbPlane(crop), nullptr, Domain::Starlet);
            std::vector<std::vector<EstimateBlock>> blocks;
            int gw = 0, gh = 0;
            double d = 1e9;
            if (eng.estimateBlocks(q, buf, half, uint32_t(W), uint32_t(H), uint32_t(W), ofx, ofx, 200, 120, blocks, gw, gh) &&
                gw == cw && gh == ch) {
              const NoiseProfile got = estimateFromBlocks(blocks, nullptr, Domain::Starlet);
              d = 0;
              for (int b = 0; b < kBands; ++b)
                for (int c = 0; c < 3; ++c) {
                  for (int k = 0; k < kBins; ++k)
                    d = std::fmax(d, std::fabs(double(got.sigma[b][c][k]) - want.sigma[b][c][k]) /
                                         std::fmax(1e-6, double(want.sigma[b][c][k])));
                  d = std::fmax(d, std::fabs(double(got.corr[b][c]) - want.corr[b][c]));
                }
            }
            if (d >= 1e-3) { std::printf("  FAIL estimate %s %dx%d mode %d: %.6f\n", name, W, H, mode, d); ++failures; }
            worstEst = std::fmax(worstEst, d);
          }
          id<MTLCommandBuffer> cb = [q commandBuffer];
          if (!eng.encode(cb, buf, buf, half, uint32_t(W), uint32_t(H), uint32_t(W), prof, sp, ofx)) ++failures;
          [cb commit];
          [cb waitUntilCompleted];
          double maxAbs = 0;
          bool alphaOk = true;
          for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
              const size_t i = size_t(ofx ? H - 1 - y : y) * W + x;
              float g[4];
              if (half) {
                const __fp16* d = static_cast<const __fp16*>(buf.contents) + i * 4;
                g[0] = d[2]; g[1] = d[1]; g[2] = d[0]; g[3] = d[3];
              } else {
                const float* d = static_cast<const float*>(buf.contents) + i * 4;
                g[0] = ofx ? d[0] : d[2]; g[1] = d[1]; g[2] = ofx ? d[2] : d[0]; g[3] = d[3];
              }
              const float* r = ref.at(x, y);
              for (int c = 0; c < 3; ++c) maxAbs = std::isfinite(g[c]) ? std::fmax(maxAbs, std::fabs(g[c] - r[c])) : 1e9;
              alphaOk = alphaOk && g[3] == r[3];
            }
          const double tol = mode == 0 ? 1e-3 : (mode == 1 ? 4.0 / 255.0 : 3.0 / 255.0);
          const bool ok = alphaOk && maxAbs <= tol;
          if (!ok) std::printf("  FAIL %s %dx%d mode %d: max %.6f, alpha %s\n", name, W, H, mode, maxAbs, alphaOk ? "ok" : "CHANGED");
          failures += ok ? 0 : 1;
          worst[mode] = std::fmax(worst[mode], maxAbs);
          ++cases;
        }
      }
    }
    std::printf("Noise estimate on Metal vs CPU: worst relative difference %.2e\n", worstEst);
    std::printf("Denoise Metal vs CPU: %d cases, worst 32f x255 = %.4f, 16f x255 = %.3f, OpenFX x255 = %.4f\n%s\n", cases,
                worst[0] * 255, worst[1] * 255, worst[2] * 255, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
  }
}
