// Temporal candidate T1 vs spatial (a) vs noisy input on the analytic sequences (src/eval/
// dn_sequence.h): RGB RMSE, detail transfer, flat residual, temporal residual and disocclusion error
// against ground truth. Oracle noise profiles (measured from frame 0's noise) isolate filter quality.
// Usage: dn_temporal_eval [width height frames noiseScale seed radius]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "dn_profile.h"
#include "dn_sequence.h"
#include "dn_sequence_metrics.h"
#include "dn_spatial.h"
#include "dn_temporal.h"

using namespace dn;
using namespace dn::eval;

namespace {
struct Acc {
  double rmse = 0, detail = 0, flat = 0, temporal = 0, disocc = 0;
  int n = 0, nd = 0, nf = 0, nt = 0, no = 0;
  void add(const FrameQuality& q, const Metric& t) {
    if (q.all.rmse.samples) { rmse += q.all.rmse.value; ++n; }
    if (q.detailTransfer.samples) { detail += q.detailTransfer.value; ++nd; }
    if (q.flatResidualSigma.samples) { flat += q.flatResidualSigma.value; ++nf; }
    if (t.samples) { temporal += t.value; ++nt; }
    if (q.disocclusionRmse.samples) { disocc += q.disocclusionRmse.value; ++no; }
  }
  void print(const char* name) const {
    auto avg = [](double s, int k) { return k ? s / k : NAN; };
    std::printf("  %-10s RMSE %.5f  detail %.3f  flat %.5f  temporal %.5f  disocc %.5f\n", name, avg(rmse, n),
                avg(detail, nd), avg(flat, nf), avg(temporal, nt), avg(disocc, no));
  }
};
}  // namespace

int main(int argc, char** argv) {
  SequenceConfig base;
  base.width = argc > 1 ? std::atoi(argv[1]) : 320;
  base.height = argc > 2 ? std::atoi(argv[2]) : 180;
  base.frameCount = argc > 3 ? std::atoi(argv[3]) : 8;
  const float noiseScale = argc > 4 ? float(std::atof(argv[4])) : 4.f;
  base.seed = argc > 5 ? unsigned(std::atoi(argv[5])) : 7u;
  const int radius = argc > 6 ? std::atoi(argv[6]) : 2;
  base.shotNoise *= noiseScale * noiseScale;
  base.readNoise *= noiseScale;
  std::printf("dn_temporal_eval %dx%d, %d frames, noise x%.1f, seed %u, temporal radius %d (%s profiles)\n",
              base.width, base.height, base.frameCount, noiseScale, base.seed, radius, std::getenv("DN_ESTIMATE") ? "estimated" : "oracle");
  for (const std::string& name : sequenceScenarioNames()) {
    SequenceConfig cfg = base;
    cfg.scenario = parseSequenceScenario(name);
    std::vector<SequenceFrame> seq;
    for (int t = 0; t < cfg.frameCount; ++t) seq.push_back(generateSequenceFrame(cfg, t));
    // Oracle (measured from frame 0's true noise) or estimated (DN_ESTIMATE=1): two-frame temporal
    // estimate from frames 0 and 1, in the denoiser's starlet domain and in the pixel domain.
    const bool estimated = std::getenv("DN_ESTIMATE") != nullptr;
    const vv::Plane r0 = toRgbPlane(seq[0].noisy), r1 = toRgbPlane(seq[1].noisy), c0 = toRgbPlane(seq[0].clean);
    const NoiseProfile trueProf = measureProfileStarlet(r0, c0);
    const PixelSigma truePs = measurePixelSigma(r0, c0);
    const NoiseProfile prof = estimated ? estimateProfileTemporal(r0, r1, nullptr, Domain::Starlet) : trueProf;
    const PixelSigma ps = estimated ? pixelSigmaFrom(estimateProfileTemporal(r0, r1, nullptr, Domain::Pixel)) : truePs;
    if (estimated) {
      // Estimate / truth at mid grey, green channel: band 0 of the starlet profile and pixel sigma.
      std::printf("  estimate/truth: starlet band0 %.2f band2 %.2f, pixel %.2f\n", prof.at(0, 1, 0.45f) / trueProf.at(0, 1, 0.45f),
                  prof.at(2, 1, 0.45f) / trueProf.at(2, 1, 0.45f), ps.at(1, 0.45f) / truePs.at(1, 0.45f));
    }
    std::vector<vv::Image> pilots;
    for (const auto& f : seq) pilots.push_back(denoiseSpatial(f.noisy, prof));
    Acc noisy, spatial, temporal;
    vv::Image prevN, prevS, prevT;
    double meanW = 0, meanG = 0;
    for (int t = 0; t < cfg.frameCount; ++t) {
      std::vector<const vv::Image*> win;
      std::vector<vv::Image> winPilots;
      int centre = 0;
      for (int k = -radius; k <= radius; ++k) {
        const int i = t + k;
        if (i < 0 || i >= cfg.frameCount) continue;
        if (k == 0) centre = int(win.size());
        win.push_back(&seq[size_t(i)].noisy);
        winPilots.push_back(pilots[size_t(i)]);
      }
      TemporalDebug dbg;
      TemporalParams tp;
      if (const char* e = std::getenv("DN_POST")) tp.postStrength = float(std::atof(e));
      const vv::Image out = denoiseTemporal(win, centre, prof, ps, {}, tp, &dbg, &winPilots);
      meanW += dbg.meanWeight;
      meanG += dbg.zScale;
      const SequenceFrame& f = seq[size_t(t)];
      auto score = [&](Acc& a, const vv::Image& img, const vv::Image& prev) {
        Metric tm;
        if (t > 0) tm = temporalResidualError(img, f.clean, prev, seq[size_t(t - 1)].clean, f.backwardFlow, f.visibleMask);
        a.add(frameQuality(img, f.clean, f.detailMask, f.flatMask, f.disocclusionMask), tm);
      };
      score(noisy, f.noisy, prevN);
      score(spatial, pilots[size_t(t)], prevS);
      score(temporal, out, prevT);
      prevN = f.noisy; prevS = pilots[size_t(t)]; prevT = out;
    }
    std::printf("%s (mean neighbour weight %.2f, z scale %.2f)\n", name.c_str(), meanW / cfg.frameCount, meanG / cfg.frameCount);
    noisy.print("noisy");
    spatial.print("spatial a");
    temporal.print("temporal T1");
  }
  return 0;
}
