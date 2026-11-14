// Denoise: shared CPU/GPU helpers and the CUDA renderer (spatial filter only; temporal fusion on the
// GPU is not implemented yet). STATUS: CUDA compiled in CI; not run in a host.
#include "DnGpu.h"

#include <algorithm>
#include <cstring>

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

SpatialParams spatialParams(const EffectParams& ep) {
  SpatialParams sp;
  sp.lumaStrength = ep.lumaStrength;
  sp.chromaStrength = ep.chromaStrength;
  return sp;
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
    vv::Image crop;
    if (!engine_->downloadCrop(f.src, f.half, f.width, f.height, f.rowPixels, f.rgba, f.bottomUp, cropW(ep),
                               cropH(ep), crop))
      return false;
    return engine_->render(f.src, f.dst, f.half, f.width, f.height, f.rowPixels,
                           estimateSpatialProfile(crop, ep.noiseLevel), spatialParams(ep), f.rgba, ep.view, ep.mix);
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
