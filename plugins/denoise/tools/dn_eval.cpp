// Denoise evaluation harness (development tool, not part of a plug-in).
//   dn_eval_tool profile   - blind noise-profile estimate vs ground truth on every test image and
//                            noise setting; prints the estimate/truth ratio per band and channel.
//   dn_eval_tool temporal  - the same with the two-frame temporal estimate (static shot plus a
//                            moving block).
//   dn_eval_tool baseline  - PSNR/SSIM of the noisy input and of a Gaussian-blur baseline.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dn_metrics.h"
#include "dn_profile.h"
#include "dn_synth.h"
#include "dn_testimages.h"
#include "vv/blur.h"

using namespace dn;
using namespace dn::eval;

namespace {

struct Setting {
  const char* name;
  NoiseSim sim;
};

std::vector<Setting> settings() {
  std::vector<Setting> s;
  NoiseSim a; a.shot = 0.0005f; a.read = 0.002f; s.push_back({"low", a});
  NoiseSim b; s.push_back({"medium", b});
  NoiseSim c; c.shot = 0.008f; c.read = 0.01f; s.push_back({"high", c});
  NoiseSim d; d.sharpen = 0.8f; s.push_back({"medium+sharpen", d});
  NoiseSim e; e.bayer = false; s.push_back({"medium, no bayer", e});
  return s;
}

int profileReport(int w, int h, bool temporal) {
  std::printf("ratio = estimate / truth (1.00 is perfect), per RGB channel: median/worst over measured bins\n");
  std::printf("%-13s %-17s | %-29s | %-29s | corr err\n", "image", "noise", "band0 R / G / B", "band1 R / G / B");
  double worstAll = 1.0, worstCorr = 0;
  for (const std::string& name : testImageNames()) {
    const vv::Image src = makeTestImage(name, w, h);
    for (const Setting& st : settings()) {
      const Synth s = synthesize(src, st.sim, 7u);
      const NoiseProfile truth = measureProfile(toRgbPlane(s.noisy), toRgbPlane(s.clean));
      EstimateStats stats;
      NoiseProfile est;
      if (temporal) {
        // Second frame of the same static shot (new noise), with a moving block (16 % of the
        // frame shifted by 9 px) so motion rejection is exercised too.
        vv::Image moved = src;
        for (int y = h / 4; y < h / 4 + h * 2 / 5; ++y)
          for (int x = w / 3; x < w / 3 + w * 2 / 5; ++x)
            for (int c = 0; c < 4; ++c) moved.at(x, y)[c] = src.at(std::min(x + 9, w - 1), y)[c];
        const Synth s2 = synthesize(moved, st.sim, 7u, 1);
        est = estimateProfileTemporal(toRgbPlane(s.noisy), toRgbPlane(s2.noisy), &stats);
      } else {
        est = estimateProfile(toRgbPlane(s.noisy), &stats);
      }
      std::printf("%-13s %-17s |", name.c_str(), st.name);
      double ce = 0;
      for (int b = 0; b < 2; ++b) {
        for (int c = 0; c < 3; ++c) {
          std::vector<double> r;
          for (int k = 0; k < kBins; ++k)
            if (stats.binMeasured[b][c][k] && stats.blocks[b][c][k] >= 40 && truth.sigma[b][c][k] > 1e-6f)
              r.push_back(est.sigma[b][c][k] / truth.sigma[b][c][k]);
          if (r.empty()) { std::printf("    -     "); continue; }
          std::sort(r.begin(), r.end());
          const double med = r[r.size() / 2];
          const double worst = std::fabs(std::log(r.front())) > std::fabs(std::log(r.back())) ? r.front() : r.back();
          worstAll = std::fabs(std::log(worst)) > std::fabs(std::log(worstAll)) ? worst : worstAll;
          std::printf(" %4.2f/%4.2f", med, worst);
        }
        std::printf(" |");
        for (int k = 0; k < 3; ++k) ce = std::fmax(ce, std::fabs(est.corr[b][k] - truth.corr[b][k]));
      }
      worstCorr = std::fmax(worstCorr, ce);
      std::printf(" %.2f\n", ce);
    }
  }
  std::printf("worst ratio (bands 0-1): %.2f, worst correlation error: %.2f\n", worstAll, worstCorr);
  return 0;
}

int baselineReport(int w, int h) {
  std::printf("%-13s %-17s | noisy PSNR / SSIM | gauss 0.8 PSNR / SSIM | gauss 1.5 PSNR / SSIM\n", "image", "noise");
  for (const std::string& name : testImageNames()) {
    const vv::Image src = makeTestImage(name, w, h);
    for (const Setting& st : settings()) {
      const Synth s = synthesize(src, st.sim, 7u);
      std::printf("%-13s %-17s | %6.2f / %.4f   ", name.c_str(), st.name, psnr(s.noisy, s.clean), ssim(s.noisy, s.clean));
      for (float sig : {0.8f, 1.5f}) {
        vv::Plane p(w, h, 4);
        p.v = s.noisy.px;
        const vv::Plane b = vv::gaussianBlurDirect(p, sig);
        vv::Image o = s.noisy;
        o.px = b.v;
        std::printf("| %6.2f / %.4f      ", psnr(o, s.clean), ssim(o, s.clean));
      }
      std::printf("\n");
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "profile";
  const int w = argc > 3 ? std::atoi(argv[2]) : 960, h = argc > 3 ? std::atoi(argv[3]) : 540;
  if (mode == "profile") return profileReport(w, h, false);
  if (mode == "temporal") return profileReport(w, h, true);
  if (mode == "baseline") return baselineReport(w, h);
  std::fprintf(stderr, "usage: dn_eval_tool profile|temporal|baseline [w h]\n");
  return 2;
}
