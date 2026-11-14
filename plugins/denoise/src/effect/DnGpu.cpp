// Denoise: shared CPU/GPU helpers and the CUDA renderer (spatial filter only; temporal fusion on the
// GPU is not implemented yet). STATUS: CUDA compiled in CI; not run in a host.
#include "DnGpu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "vv/overlay.h"

#ifdef DN_WITH_CUDA_RENDERER
#include "DnCuda.h"
#endif

namespace dn {

vv::Image centreCrop(const vv::Image& img, int maxW, int maxH) {
  const int cw = std::min(img.width, maxW), ch = std::min(img.height, maxH);
  if (cw == img.width && ch == img.height) return img;
  const int x0 = (img.width - cw) / 2, y0 = (img.height - ch) / 2;
  vv::Image out(cw, ch);
  for (int y = 0; y < ch; ++y) std::memcpy(out.at(0, y), img.at(x0, y0 + y), size_t(cw) * 4 * sizeof(float));
  return out;
}

NoiseProfile estimateSpatialProfile(const vv::Image& crop, float level) {
  return estimateProfile(toRgbPlane(crop), nullptr, Domain::Starlet).scaled(level);
}

NoiseProfile profileFromBlocks(const std::vector<std::vector<EstimateBlock>>& blocks, float level) {
  return estimateFromBlocks(blocks, nullptr, Domain::Starlet).scaled(level);
}

SpatialParams spatialParams(const EffectParams& ep) {
  SpatialParams sp;
  sp.lumaStrength = ep.lumaStrength;
  sp.chromaStrength = ep.chromaStrength;
  return sp;
}

double nowMs() {
  using C = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(C::now().time_since_epoch()).count();
}

std::vector<std::string> timingLines(const Timing& t) {
  char b[5][96];
  std::snprintf(b[0], sizeof b[0], "DENOISE TIMING - %s", t.path);
  std::snprintf(b[1], sizeof b[1], "%dX%d %s - QUALITY %s", t.width, t.height, t.half ? "16F" : "32F",
                t.preview ? "PREVIEW" : "FINAL");
  std::snprintf(b[2], sizeof b[2], "NOISE ESTIMATE %.0f MS (%dX%d)", t.estimateMs, t.cropW, t.cropH);
  std::snprintf(b[3], sizeof b[3], "FILTER %.0f MS", t.filterMs);
  std::snprintf(b[4], sizeof b[4], "TOTAL %.0f MS", t.totalMs);
  return {b[0], b[1], b[2], b[3], b[4]};
}

void timingBox(const Timing& t, int frameWidth, int frameHeight, int& w, int& h) {
  vv::overlay::bannerSize(timingLines(t), vv::overlay::bannerScale(frameHeight), w, h);
  w = std::min(w, frameWidth);
  h = std::min(h, frameHeight);
}

void drawTiming(vv::Image& img, const Timing& t, int frameHeight) {
  vv::overlay::drawBanner(img, timingLines(t), vv::overlay::bannerScale(frameHeight));
}

void flipRows(vv::Image& img) {
  for (int y = 0; y < img.height / 2; ++y)
    std::swap_ranges(img.at(0, y), img.at(0, y) + size_t(img.width) * 4, img.at(0, img.height - 1 - y));
}

#ifdef DN_WITH_CUDA_RENDERER
namespace {
class CudaRenderer : public vv::GpuRenderer {
 public:
  bool prepare(void*) override {
    if (!engine_) engine_ = std::make_unique<DnCudaEngine>();  // on the host's current CUDA context
    return engine_->ok();
  }
  bool render(const vv::GpuFrame& f, const vv::ParamReader& r, int64_t) override {
    if (!prepare(nullptr)) return false;
    const EffectParams ep = readParams(r);
    const double t0 = nowMs();
    std::vector<std::vector<EstimateBlock>> blocks;
    int cw = 0, ch = 0;
    if (!engine_->estimateBlocks(f.src, f.half, f.width, f.height, f.rowPixels, f.rgba, f.bottomUp, cropW(ep),
                                 cropH(ep), blocks, cw, ch))
      return false;
    const NoiseProfile prof = profileFromBlocks(blocks, ep.noiseLevel);
    const double t1 = nowMs();
    const bool timing = ep.view == 3;
    if (!engine_->render(f.src, f.dst, f.half, f.width, f.height, f.rowPixels, prof, spatialParams(ep), f.rgba,
                         timing ? 0 : ep.view, ep.mix))
      return false;
    if (!timing) return true;
    const double t2 = nowMs();
    const Timing tm{"CUDA", int(f.width), int(f.height), cw, ch, f.half, ep.preview,
                    t1 - t0, t2 - t1, t2 - t0};
    int bw = 0, bh = 0;
    timingBox(tm, int(f.width), int(f.height), bw, bh);
    const uint32_t y0 = f.bottomUp ? f.height - uint32_t(bh) : 0;
    vv::Image box;
    if (!engine_->readRegion(f.dst, f.half, f.rowPixels, f.rgba, 0, y0, uint32_t(bw), uint32_t(bh), box)) return false;
    if (f.bottomUp) flipRows(box);
    drawTiming(box, tm, int(f.height));
    if (f.bottomUp) flipRows(box);
    return engine_->writeRegion(f.dst, f.half, f.rowPixels, f.rgba, 0, y0, box);
  }

 private:
  std::unique_ptr<DnCudaEngine> engine_;
};
}  // namespace
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return std::make_unique<CudaRenderer>(); }
#else
std::unique_ptr<vv::GpuRenderer> makeCudaRenderer() { return nullptr; }
#endif

#ifndef DN_WITH_METAL_RENDERER
std::unique_ptr<vv::GpuRenderer> makeMetalRenderer() { return nullptr; }
#endif

}  // namespace dn
