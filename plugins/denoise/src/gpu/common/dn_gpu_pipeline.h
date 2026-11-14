// Denoise — spatial filter as GPU passes for any backend (CUDA, Metal, CPU emulation). Same steps
// and blur plans as dn_spatial.cpp. The Backend provides the vv::gpu::Pyramid operations plus:
//   void dnPrepare(const void* src, bool half, Buf opp, const FrameLayout&);
//   void dnStarletH(Buf src, Buf tmp, PlaneInfo, int step);
//   void dnStarletVBand(Buf tmp, Buf cur, Buf next, Buf band, Buf energy, Buf var, const DnProfileTable&,
//                       const BandInfo&, int step);
//   void dnPilot(Buf band, Buf E, Buf var, Buf pilot2, const BandInfo&);
//   void dnAccumulate(Buf out, Buf band, Buf P, Buf var, const BandInfo&);
//   void dnFinish(const void* src, void* dst, bool half, Buf out, Buf residual, const FrameLayout&);
// and for the noise estimate on the device:
//   void dnCrop(const void* src, bool half, Buf rgb, const CropInfo&);
//   void dnStarletV(Buf src, Buf dst, PlaneInfo, int step);
//   void dnBlocks(Buf cur, Buf next, Buf blocks, size_t offsetF4, const BlockGrid&);  // 3 F4 per block
// and for Quality Preview (half resolution):
//   void dnHalve(const void* src, bool half, Buf dst, const FrameLayout& full, PlaneInfo halfSize);
//   void dnUpFinish(const void* src, void* dst, bool half, Buf low, const FrameLayout& full, PlaneInfo halfSize);
//   static const void* ptr(Buf);  static void* mptr(Buf);   // a Buf as a frame pointer
// Preview runs the same filter on the 2x2-averaged frame and estimates the noise on that frame
// too (its noise is lower and coarser than the full frame's), then upsamples the result.
// Per band: 4 full-frame passes + 2 pyramid blurs. Memory (F4 full-res buffers): 9 + the pyramid's scratch. Rows may be bottom-up (OFX): every
// pass is symmetric in y, so results do not depend on row order.
#pragma once
#include "dn_gpu_pixels.h"
#include "dn_profile.h"
#include "dn_spatial.h"
#include "gpu/vv_gpu_pyramid.h"
#include "vv/blur_plan.h"

namespace dn {
namespace gpu {

inline DnProfileTable toTable(const NoiseProfile& p) {
  DnProfileTable t{};
  for (int b = 0; b < DN_BANDS; ++b)
    for (int c = 0; c < 3; ++c) {
      for (int k = 0; k < DN_BINS; ++k) t.sigma[(b * 3 + c) * DN_BINS + k] = p.sigma[b][c][k];
      t.corr[b * 3 + c] = p.corr[b][c];
    }
  return t;
}

/** Unpacks the block buffer of encodeEstimate (bands one after another) into the CPU layout. */
inline std::vector<std::vector<EstimateBlock>> unpackBlocks(const F4* data, uint32_t perBand, int bands) {
  std::vector<std::vector<EstimateBlock>> out(static_cast<size_t>(bands));
  for (int b = 0; b < bands; ++b) {
    out[size_t(b)].resize(perBand);
    for (uint32_t i = 0; i < perBand; ++i) {
      const F4* q = data + (size_t(b) * perBand + i) * 3;
      EstimateBlock& e = out[size_t(b)][i];
      e.var[0] = q[0].x; e.var[1] = q[0].y; e.var[2] = q[0].z;
      e.value[0] = q[1].x; e.value[1] = q[1].y; e.value[2] = q[1].z;
      e.cov[0] = q[2].x; e.cov[1] = q[2].y; e.cov[2] = q[2].z;
    }
  }
  return out;
}

template <typename Backend>
class SpatialPipeline {
 public:
  using Buf = typename Backend::Buf;
  explicit SpatialPipeline(Backend& b) : B(b), pyr(b) {}

  /** Encodes the device part of the noise estimate on a centred crop (at most maxW x maxH of a
   *  w x h frame). Afterwards blocks() holds blockCount() blocks per band, DN_BANDS bands, 3 F4
   *  each; read it back once the work has finished and pass it to unpackBlocks. */
  bool encodeEstimate(const void* src, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, bool rgba, bool bottomUp,
                      uint32_t maxW, uint32_t maxH, bool preview = false) {
    if (w == 0 || h == 0) return false;
    const uint32_t pw = preview ? (w + 1) / 2 : w, ph = preview ? (h + 1) / 2 : h;
    const uint32_t cw = pw < maxW ? pw : maxW, ch = ph < maxH ? ph : maxH;
    if (cw < uint32_t(kEstimateBlock) || ch < uint32_t(kEstimateBlock)) return false;
    ensure(w, h, cw, ch, preview);
    const void* from = src;
    bool fromHalf = half;
    uint32_t fromRow = rowPixels;
    if (preview) {
      B.dnHalve(src, half, hsrc, FrameLayout{w, h, rowPixels, rgba ? 1 : 0, 0, 1.0f}, PlaneInfo{pw, ph});
      from = Backend::ptr(hsrc);
      fromHalf = false;
      fromRow = pw;
    }
    const CropInfo c{cw, ch, ph, fromRow, (pw - cw) / 2, (ph - ch) / 2, rgba ? 1 : 0, bottomUp ? 1 : 0};
    const PlaneInfo p{cw, ch};
    B.dnCrop(from, fromHalf, cropA, c);
    Buf cur = cropA, next = cropB;
    for (int j = 0; j < DN_BANDS; ++j) {
      B.dnStarletH(cur, cropT, p, 1 << j);
      B.dnStarletV(cropT, next, p, 1 << j);
      B.dnBlocks(cur, next, blockBuf, size_t(j) * blockCount() * 3, grid);
      std::swap(cur, next);
    }
    return true;
  }
  uint32_t cropWidth() const { return cropW; }
  uint32_t cropHeight() const { return cropH; }
  uint32_t blockCount() const { return grid.cols * grid.rows; }
  Buf blocks() const { return blockBuf; }

  bool run(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels, const NoiseProfile& prof,
           const SpatialParams& sp, bool rgba, int view = 0, float mix = 1.0f, bool preview = false) {
    if (w == 0 || h == 0) return false;
    ensure(w, h, cropW, cropH, preview);
    const FrameLayout full{w, h, rowPixels, rgba ? 1 : 0, view, mix};
    if (!preview) {
      filter(src, dst, half, w, h, rowPixels, prof, sp, full);
      return true;
    }
    const uint32_t pw = (w + 1) / 2, ph = (h + 1) / 2;
    B.dnHalve(src, half, hsrc, full, PlaneInfo{pw, ph});
    filter(Backend::ptr(hsrc), Backend::mptr(hdst), false, pw, ph, pw, prof, sp,
           FrameLayout{pw, ph, pw, rgba ? 1 : 0, 0, 1.0f});
    B.dnUpFinish(src, dst, half, hdst, full, PlaneInfo{pw, ph});
    return true;
  }

 private:
  Backend& B;
  vv::gpu::Pyramid<Backend> pyr;
  uint32_t cachedW = 0, cachedH = 0, cropW = 0, cropH = 0;
  bool cachedPreview = false;
  Buf oppA{}, oppB{}, tmp{}, band{}, energy{}, E{}, var{}, pilot2{}, out{};
  Buf cropA{}, cropB{}, cropT{}, blockBuf{}, hsrc{}, hdst{};

  // The spatial filter on a w x h frame (host layout f; f.width/height == w/h).
  void filter(const void* src, void* dst, bool half, uint32_t w, uint32_t h, uint32_t rowPixels,
              const NoiseProfile& prof, const SpatialParams& sp, const FrameLayout& f) {
    (void)rowPixels;
    const PlaneInfo p{w, h};
    const DnProfileTable table = toTable(prof);
    B.dnPrepare(src, half, oppA, f);
    B.clear(out, p);
    Buf cur = oppA, next = oppB;
    for (int j = 0; j < DN_BANDS; ++j) {
      const BandInfo bi{w, h, j, 0, sp.pilotTau, {sp.lumaStrength, sp.chromaStrength, sp.chromaStrength}, sp.detailRecovery};
      B.dnStarletH(cur, tmp, p, 1 << j);
      B.dnStarletVBand(tmp, cur, next, band, energy, var, table, bi, 1 << j);
      const vv::BlurStep plan = vv::planGaussian(sp.window * float(1 << j), int(w), int(h));
      pyr.blurByPlan(energy, E, plan);
      B.dnPilot(band, E, var, pilot2, bi);
      pyr.blurByPlan(pilot2, E, plan);  // E now holds P
      B.dnAccumulate(out, band, E, var, bi);
      std::swap(cur, next);
    }
    B.dnFinish(src, dst, half, out, cur, f);
  }
  BlockGrid grid{0, 0, 0, 0};

  // One allocation set for the frame (full or, for Preview, half size) and the estimate crop
  // (releaseAll frees everything).
  void ensure(uint32_t w, uint32_t h, uint32_t cw, uint32_t ch, bool preview) {
    if (w == cachedW && h == cachedH && cw == cropW && ch == cropH && preview == cachedPreview) return;
    cachedW = w;
    cachedH = h;
    cropW = cw;
    cropH = ch;
    cachedPreview = preview;
    B.releaseAll();
    const uint32_t pw = preview ? (w + 1) / 2 : w, ph = preview ? (h + 1) / 2 : h;
    const size_t n = size_t(pw) * ph;
    oppA = B.alloc(n); oppB = B.alloc(n); tmp = B.alloc(n);
    band = B.alloc(n); energy = B.alloc(n); E = B.alloc(n); var = B.alloc(n); pilot2 = B.alloc(n); out = B.alloc(n);
    pyr.allocate(pw, ph);
    hsrc = hdst = Buf{};
    if (preview) {
      hsrc = B.alloc(n);
      hdst = B.alloc(n);
    }
    grid = BlockGrid{cw, ch, 0, 0};
    blockBuf = Buf{};
    if (cw >= uint32_t(kEstimateBlock) && ch >= uint32_t(kEstimateBlock)) {
      grid.cols = (cw - uint32_t(kEstimateBlock)) / uint32_t(kEstimateStride) + 1;
      grid.rows = (ch - uint32_t(kEstimateBlock)) / uint32_t(kEstimateStride) + 1;
      const size_t c = size_t(cw) * ch;
      cropA = B.alloc(c); cropB = B.alloc(c); cropT = B.alloc(c);
      blockBuf = B.alloc(size_t(blockCount()) * 3 * DN_BANDS);
    }
  }
};

}  // namespace gpu
}  // namespace dn
