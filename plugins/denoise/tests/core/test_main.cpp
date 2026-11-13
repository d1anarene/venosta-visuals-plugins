// Denoise tests (research phase): colour transform, pyramid, noise synthesis, metrics, profile.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "dn_color.h"
#include "dn_metrics.h"
#include "dn_profile.h"
#include "dn_pyramid.h"
#include "dn_spatial.h"
#include "dn_starlet.h"
#include "dn_synth.h"
#include "dn_testimages.h"
#include "vv/blur.h"

using namespace dn;
using namespace dn::eval;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++g_fail;                                                        \
    } else {                                                           \
      ++g_pass;                                                        \
    }                                                                  \
  } while (0)
static std::vector<std::pair<std::string, std::function<void()>>>& registry() {
  static std::vector<std::pair<std::string, std::function<void()>>> r;
  return r;
}
struct Reg {
  Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
#define TEST(name)                    \
  static void name();                 \
  static Reg reg_##name(#name, name); \
  static void name()

static vv::Image grey(int w, int h, float v) {
  vv::Image im(w, h);
  for (int i = 0; i < w * h; ++i) { im.px[i * 4] = im.px[i * 4 + 1] = im.px[i * 4 + 2] = v; im.px[i * 4 + 3] = 1.f; }
  return im;
}

TEST(opponent_is_orthonormal) {
  for (float r : {0.f, 0.3f, 1.f, 2.5f})
    for (float g : {0.f, 0.7f})
      for (float b : {0.f, 0.2f, 1.f}) {
        const vv_float3 o = dn_to_opponent(vv_make3(r, g, b)), c = dn_from_opponent(o);
        CHECK(std::fabs(c.x - r) < 1e-5f && std::fabs(c.y - g) < 1e-5f && std::fabs(c.z - b) < 1e-5f);
        CHECK(std::fabs(o.x * o.x + o.y * o.y + o.z * o.z - (r * r + g * g + b * b)) < 1e-4f);  // norm kept
      }
  const vv_float3 o = dn_to_opponent(vv_make3(0.4f, 0.4f, 0.4f));
  CHECK(std::fabs(o.y) < 1e-6f && std::fabs(o.z) < 1e-6f && std::fabs(o.x * DN_INV_SQRT3 - 0.4f) < 1e-6f);
}

TEST(pyramid_reconstructs_exactly) {
  for (auto [w, h] : std::vector<std::pair<int, int>>{{1, 1}, {7, 3}, {97, 61}, {256, 128}}) {
    const vv::Image im = makeTestImage("foliage", w, h);
    const vv::Plane o = toOpponent(im);
    const Pyramid p = decompose(o, kBands);
    const vv::Plane r = reconstruct(p);
    double m = 0;
    for (size_t i = 0; i < o.v.size(); ++i) m = std::fmax(m, std::fabs(double(r.v[i]) - o.v[i]));
    CHECK(m < 1e-5);
  }
  const Pyramid flat = decompose(toOpponent(grey(64, 64, 0.3f)), kBands);
  double m = 0;
  for (int b = 0; b < kBands; ++b)
    for (float v : flat.bands[size_t(b)].v) m = std::fmax(m, std::fabs(v));
  CHECK(m < 1e-6);
}

TEST(synthesis_is_deterministic_and_has_the_modelled_variance) {
  const vv::Image src = grey(256, 256, 0.25f);
  NoiseSim sim;
  sim.bayer = false;
  sim.matrix = false;
  sim.space = VV_SPACE_LINEAR;
  sim.shot = 0.004f;
  sim.read = 0.01f;
  const Synth a = synthesize(src, sim, 3u, 10), b = synthesize(src, sim, 3u, 10), c = synthesize(src, sim, 3u, 11);
  CHECK(a.noisy.px == b.noisy.px && a.noisy.px != c.noisy.px);
  CHECK(a.clean.px == src.px);
  double s = 0, s2 = 0;
  const int n = 256 * 256;
  for (int i = 0; i < n; ++i) { const double d = a.noisy.px[i * 4 + 1] - 0.25; s += d; s2 += d * d; }
  const double var = s2 / n - (s / n) * (s / n), expect = 0.004 * 0.25 + 0.01 * 0.01;
  CHECK(std::fabs(s / n) < 0.002);
  CHECK(std::fabs(var / expect - 1.0) < 0.03);
  NoiseSim none;
  none.shot = none.read = 0.f;
  const Synth z = synthesize(makeTestImage("chart", 64, 36), none, 1u);
  CHECK(z.noisy.px == z.clean.px);
}

TEST(bayer_noise_is_spatially_and_channel_correlated) {
  NoiseSim sim;
  sim.space = VV_SPACE_LINEAR;
  const Synth s = synthesize(grey(256, 256, 0.3f), sim, 5u);
  double c01 = 0, cRG = 0, v = 0;
  for (int y = 1; y < 255; ++y)
    for (int x = 1; x < 254; ++x) {
      const double a = s.noisy.at(x, y)[1] - s.clean.at(x, y)[1], b = s.noisy.at(x + 1, y)[1] - s.clean.at(x + 1, y)[1];
      const double r = s.noisy.at(x, y)[0] - s.clean.at(x, y)[0];
      c01 += a * b; cRG += a * r; v += a * a;
    }
  CHECK(c01 / v > 0.2);  // neighbouring pixels correlated (white noise: ~0)
  CHECK(std::fabs(cRG) / v > 0.1);  // channels correlated (colour matrix)
}

TEST(metrics_behave) {
  const vv::Image a = makeTestImage("weave", 128, 72);
  CHECK(std::isinf(psnr(a, a)));
  CHECK(std::fabs(ssim(a, a) - 1.0) < 1e-6);
  vv::Image b = a;
  for (size_t i = 0; i < b.px.size(); ++i)
    if (i % 4 != 3) b.px[i] += 0.1f;
  CHECK(std::fabs(psnr(b, a) - 20.0) < 1e-3);
  NoiseSim lo, hi;
  lo.shot = 0.0005f; lo.read = 0.002f;
  hi.shot = 0.008f; hi.read = 0.01f;
  const Synth sl = synthesize(a, lo, 1u), sh = synthesize(a, hi, 1u);
  CHECK(psnr(sl.noisy, sl.clean) > psnr(sh.noisy, sh.clean) + 5);
  CHECK(ssim(sl.noisy, sl.clean) > ssim(sh.noisy, sh.clean));
}

TEST(engine_blur_supports_any_channel_count) {
  // Regression: gaussianBlurDirect used a 3-entry accumulator (stack overflow with 4+ channels).
  vv::Plane p(37, 23, 5);
  for (size_t i = 0; i < p.v.size(); ++i) p.v[i] = float(vv_unit(vv_hash(vv_uint(i))));
  const vv::Plane b = vv::gaussianBlurDirect(p, 1.5f);
  bool same = true;
  for (int c = 0; c < 5; ++c) {
    vv::Plane one(37, 23, 1);
    for (int i = 0; i < 37 * 23; ++i) one.v[size_t(i)] = p.v[size_t(i) * 5 + size_t(c)];
    const vv::Plane bo = vv::gaussianBlurDirect(one, 1.5f);
    for (int i = 0; i < 37 * 23; ++i) same = same && bo.v[size_t(i)] == b.v[size_t(i) * 5 + size_t(c)];
  }
  CHECK(same);
}

TEST(profile_interpolates_between_bin_centres) {
  NoiseProfile p;
  for (int k = 0; k < kBins; ++k) p.sigma[1][2][k] = float(k);
  CHECK(std::fabs(p.at(1, 2, 0.5f / kBins) - 0.f) < 1e-6f);
  CHECK(std::fabs(p.at(1, 2, 1.0f / kBins) - 0.5f) < 1e-6f);
  CHECK(std::fabs(p.at(1, 2, -3.f) - 0.f) < 1e-6f && std::fabs(p.at(1, 2, 7.f) - float(kBins - 1)) < 1e-6f);
  CHECK(std::fabs(p.at(1, 2, NAN)) < 1e-6f);
  CHECK(std::fabs(p.scaled(2.f).at(1, 2, 7.f) - 2.f * float(kBins - 1)) < 1e-6f);
}

// The model predicts the opponent-channel noise of a flat coloured patch (W S W^T).
TEST(profile_predicts_opponent_noise_of_coloured_patches) {
  for (vv_float3 col : {vv_make3(0.8f, 0.3f, 0.1f), vv_make3(0.2f, 0.5f, 0.9f), vv_make3(0.5f, 0.5f, 0.5f)}) {
    vv::Image src(256, 256);
    for (int i = 0; i < 256 * 256; ++i) { src.px[size_t(i) * 4] = col.x; src.px[size_t(i) * 4 + 1] = col.y; src.px[size_t(i) * 4 + 2] = col.z; src.px[size_t(i) * 4 + 3] = 1; }
    const Synth s = synthesize(src, NoiseSim(), 4u);
    const NoiseProfile prof = measureProfile(toRgbPlane(s.noisy), toRgbPlane(s.clean));
    vv::Plane d = toOpponent(s.noisy);
    const vv::Plane c = toOpponent(s.clean);
    for (size_t i = 0; i < d.v.size(); ++i) d.v[i] -= c.v[i];
    const Pyramid p = decompose(d, kBands);
    for (int b = 0; b < 2; ++b) {
      const float* cc = s.clean.at(128, 128);
      const vv_float3 pred = prof.opponentSigma(b, vv_make3(cc[0], cc[1], cc[2]));
      double s2[3] = {};
      const vv::Plane& band = p.bands[size_t(b)];
      for (size_t i = 0; i < band.v.size(); ++i) s2[i % 3] += double(band.v[i]) * band.v[i];
      const double n = double(band.v.size() / 3);
      const float pr[3] = {pred.x, pred.y, pred.z};
      for (int k = 0; k < 3; ++k) {
        const double meas = std::sqrt(s2[k] / n), r = pr[k] / meas;
        if (!(r > 0.95 && r < 1.05)) std::printf("    band %d opp %d: predicted %.5f measured %.5f\n", b, k, pr[k], meas);
        CHECK(r > 0.95 && r < 1.05);
      }
    }
  }
}

// Blind estimate vs ground truth where the method is valid: frames with flat areas. Per band
// (0-1) and channel, the median ratio over the well-populated bins; correlation per band.
TEST(blind_estimate_matches_ground_truth_on_frames_with_flat_areas) {
  for (float scale : {0.5f, 1.f, 3.f})
    for (bool sharpen : {false, true}) {
      NoiseSim sim;
      sim.shot *= scale; sim.read *= scale; sim.sharpen = sharpen ? 0.8f : 0.f;
      const Synth s = synthesize(makeTestImage("skin", 960, 540), sim, 9u);
      const NoiseProfile truth = measureProfile(toRgbPlane(s.noisy), toRgbPlane(s.clean));
      EstimateStats st;
      const NoiseProfile est = estimateProfile(toRgbPlane(s.noisy), &st);
      for (int b = 0; b < 2; ++b) {
        for (int c = 0; c < 3; ++c) {
          std::vector<float> r;
          for (int k = 0; k < kBins; ++k)
            if (st.binMeasured[b][c][k] && st.blocks[b][c][k] >= 40) r.push_back(est.sigma[b][c][k] / truth.sigma[b][c][k]);
          CHECK(r.size() >= 3);
          std::sort(r.begin(), r.end());
          const float med = r.empty() ? 0.f : r[r.size() / 2];
          // Bayer-correlated noise reads 0.86-0.98 of the truth on flat patches (the calibration
          // assumes white noise; docs/RESEARCH.md), a little lower at very high noise.
          if (!(med > 0.77f && med < 1.15f)) std::printf("    x%.1f%s band %d ch %d: median ratio %.2f\n", scale, sharpen ? " sharpen" : "", b, c, med);
          CHECK(med > 0.77f && med < 1.15f);
        }
        for (int k = 0; k < 3; ++k) CHECK(std::fabs(est.corr[b][k] - truth.corr[b][k]) < 0.15f);
      }
    }
}

// Text: the flat background bin is measured correctly; edge-only bins are bounded by it.
TEST(blind_estimate_text_background_and_edge_bound) {
  const Synth s = synthesize(makeTestImage("text", 960, 540), NoiseSim(), 7u);
  const NoiseProfile truth = measureProfile(toRgbPlane(s.noisy), toRgbPlane(s.clean));
  const NoiseProfile est = estimateProfile(toRgbPlane(s.noisy));
  const float bg = 0.85f;  // background value
  for (int c = 0; c < 3; ++c) {
    const float r = est.at(0, c, bg) / truth.at(0, c, bg);
    // Green within 15 %. Red/blue (1/4 of the Bayer sites) read low: background pixels near the
    // strokes carry demosaiced noise of dark neighbours (mixed populations, docs/RESEARCH.md).
    CHECK(c == 1 ? (r > 0.85f && r < 1.15f) : (r > 0.5f && r < 1.15f));
    for (float v : {0.5f, 0.6f, 0.7f}) CHECK(est.at(0, c, v) < 4.f * truth.at(0, c, v));  // was 20-40x before the bound
  }
}

// Temporal estimate: texture cancels in static areas, so textured frames (where the spatial
// estimate fails by 3-40x) are measured correctly; a moving block is rejected.
TEST(temporal_estimate_handles_texture_and_motion) {
  for (const char* name : {"weave", "foliage", "text"}) {
    const int w = 960, h = 540;
    const vv::Image src = makeTestImage(name, w, h);
    vv::Image moved = src;
    for (int y = h / 4; y < h / 4 + h * 2 / 5; ++y)
      for (int x = w / 3; x < w / 3 + w * 2 / 5; ++x)
        for (int c = 0; c < 4; ++c) moved.at(x, y)[c] = src.at(std::min(x + 9, w - 1), y)[c];
    NoiseSim sim;
    const Synth a = synthesize(src, sim, 7u, 0), b = synthesize(moved, sim, 7u, 1);
    const NoiseProfile truth = measureProfile(toRgbPlane(a.noisy), toRgbPlane(a.clean));
    EstimateStats st;
    const NoiseProfile est = estimateProfileTemporal(toRgbPlane(a.noisy), toRgbPlane(b.noisy), &st);
    const NoiseProfile spatial = estimateProfile(toRgbPlane(a.noisy));
    for (int bnd = 0; bnd < 2; ++bnd)
      for (int c = 0; c < 3; ++c) {
        std::vector<float> r, rs;
        for (int k = 0; k < kBins; ++k)
          if (st.binMeasured[bnd][c][k] && st.blocks[bnd][c][k] >= 40) {
            r.push_back(est.sigma[bnd][c][k] / truth.sigma[bnd][c][k]);
            rs.push_back(spatial.sigma[bnd][c][k] / truth.sigma[bnd][c][k]);
          }
        CHECK(!r.empty());
        if (r.empty()) continue;
        std::sort(r.begin(), r.end());
        std::sort(rs.begin(), rs.end());
        const float med = r[r.size() / 2], medS = rs[rs.size() / 2];
        if (!(med > 0.65f && med < 1.3f)) std::printf("    %s band %d ch %d: temporal %.2f (spatial %.2f)\n", name, bnd, c, med, medS);
        CHECK(med > 0.65f && med < 1.3f);  // measured: 0.70-1.07 (weave red band 0 lowest)
        (void)medS;
      }
  }
}

TEST(noise_free_frame_estimates_almost_zero) {
  const NoiseProfile est = estimateProfile(toRgbPlane(makeTestImage("skin", 480, 270)));
  for (int c = 0; c < 3; ++c) CHECK(est.at(0, c, 0.5f) < 0.004f);  // texture floor << typical noise (~0.01)
}

TEST(starlet_reconstructs_exactly_and_is_shift_invariant) {
  const vv::Plane o = toOpponent(makeTestImage("foliage", 97, 61));
  const Starlet st = starletDecompose(o, kBands);
  const vv::Plane r = starletReconstruct(st);
  double m = 0;
  for (size_t i = 0; i < o.v.size(); ++i) m = std::fmax(m, std::fabs(double(r.v[i]) - o.v[i]));
  CHECK(m < 1e-5);
  CHECK(int(st.bands.size()) == kBands && st.bands[0].width == 97);  // undecimated: full size
  // Shifting the input by one pixel shifts every band by one pixel (away from the borders).
  vv::Plane sh = o;
  for (int y = 0; y < 61; ++y)
    for (int x = 1; x < 97; ++x)
      for (int c = 0; c < 3; ++c) sh.at(x, y)[c] = o.at(x - 1, y)[c];
  const Starlet ss = starletDecompose(sh, kBands);
  double d = 0;
  for (int y = 20; y < 41; ++y)
    for (int x = 40; x < 60; ++x) d = std::fmax(d, std::fabs(ss.bands[1].at(x, y)[0] - st.bands[1].at(x - 1, y)[0]));
  CHECK(d < 1e-5);
}

TEST(spatial_denoiser_removes_noise_keeps_signal_alpha_and_identity) {
  NoiseSim sim;
  vv::Image grey(256, 144);
  for (int i = 0; i < 256 * 144; ++i) { grey.px[size_t(i) * 4] = grey.px[size_t(i) * 4 + 1] = grey.px[size_t(i) * 4 + 2] = 0.45f; grey.px[size_t(i) * 4 + 3] = 0.7f; }
  const Synth s = synthesize(grey, sim, 3u);
  const NoiseProfile prof = measureProfileStarlet(toRgbPlane(s.noisy), toRgbPlane(s.clean));
  const vv::Image d = denoiseSpatial(s.noisy, prof);
  // Residual noise on a flat field drops to under a third (|error| RMS).
  CHECK(psnr(d, s.clean) > psnr(s.noisy, s.clean) + 10.0);
  bool alpha = true, finite = true;
  for (size_t i = 0; i < d.px.size(); ++i) {
    if (i % 4 == 3) alpha = alpha && d.px[i] == s.noisy.px[i];
    finite = finite && std::isfinite(d.px[i]);
  }
  CHECK(alpha && finite);
  // A zero profile (no noise) leaves the image unchanged up to float rounding.
  const vv::Image src = makeTestImage("text", 160, 90);
  const vv::Image same = denoiseSpatial(src, NoiseProfile{});
  double m = 0;
  for (size_t i = 0; i < src.px.size(); ++i) m = std::fmax(m, std::fabs(double(same.px[i]) - src.px[i]));
  CHECK(m < 1e-5);
  // Detail priority: on fine text it never ends below the noisy input (oracle profile).
  const Synth t = synthesize(makeTestImage("text", 480, 270), sim, 5u);
  const vv::Image dt = denoiseSpatial(t.noisy, measureProfileStarlet(toRgbPlane(t.noisy), toRgbPlane(t.clean)));
  CHECK(psnr(dt, t.clean) > psnr(t.noisy, t.clean) + 1.0);
}

int main() {
  for (auto& [name, fn] : registry()) {
    const int before = g_fail;
    fn();
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name.c_str());
  }
  std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
