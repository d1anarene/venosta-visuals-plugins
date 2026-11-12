// Film Effects core tests. No dependencies: build with CMake and run fe_core_tests.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

#include "blur/blur.h"
#include "color/pixel_io.h"
#include "common/fe_kernels.h"
#include "gate/gate.h"
#include "pipeline/pipeline.h"
#include "FilmEffectsParams.h"
#include "vv/effect.h"

using namespace fe;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    if (!(cond)) {                                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                    \
      ++g_fail;                                                                        \
    } else {                                                                           \
      ++g_pass;                                                                        \
    }                                                                                  \
  } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

static std::vector<std::pair<std::string, std::function<void()>>>& registry() {
  static std::vector<std::pair<std::string, std::function<void()>>> r;
  return r;
}
struct Reg {
  Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
#define TEST(name)                     \
  static void name();                  \
  static Reg reg_##name(#name, name);  \
  static void name()

// ---------- fixtures ----------
static Image solid(int w, int h, float r, float g, float b, float a = 1.f) {
  Image im(w, h);
  for (int i = 0; i < w * h; ++i) {
    im.px[i * 4] = r; im.px[i * 4 + 1] = g; im.px[i * 4 + 2] = b; im.px[i * 4 + 3] = a;
  }
  return im;
}
static void rect(Image& im, int x0, int y0, int x1, int y1, float r, float g, float b) {
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      float* p = im.at(x, y);
      p[0] = r; p[1] = g; p[2] = b;
    }
}
static bool allFinite(const Image& im) {
  return std::all_of(im.px.begin(), im.px.end(), [](float v) { return std::isfinite(v); });
}
static bool sameImage(const Image& a, const Image& b) { return a.px == b.px; }
static FilmParams allOn() {
  FilmParams p;
  p.bloom.enabled = p.halation.enabled = p.grain.enabled = true;
  return p;
}

// ---------- bypass & isolation ----------
TEST(bypass_master_off_is_bit_exact) {
  Image im = solid(37, 23, 0.3f, 0.6f, 0.9f);
  rect(im, 5, 5, 10, 10, 4.f, 4.f, 4.f);
  Image ref = im;
  FilmParams p = allOn();
  p.masterEnable = false;
  CHECK(process(im, p, {}));
  CHECK(sameImage(im, ref));
}
TEST(bypass_mix_zero_and_all_off_are_bit_exact) {
  Image im = solid(16, 9, 0.2f, 0.5f, 0.7f);
  Image ref = im;
  FilmParams p = allOn();
  p.mix = 0.f;
  process(im, p, {});
  CHECK(sameImage(im, ref));
  FilmParams off;
  process(im, off, {});
  CHECK(sameImage(im, ref));
  FilmParams zeroAmounts = allOn();
  zeroAmounts.bloom.intensity = zeroAmounts.halation.intensity = zeroAmounts.grain.amount = 0.f;
  process(im, zeroAmounts, {});
  CHECK(sameImage(im, ref));
}
TEST(below_threshold_image_unchanged_by_bloom_and_halation) {
  // Mid grey is far below both thresholds: bloom/halation must not touch it at all.
  Image im = solid(64, 36, 0.45f, 0.45f, 0.45f);
  Image ref = im;
  FilmParams p;
  p.bloom.enabled = p.halation.enabled = true;
  process(im, p, {});
  CHECK(sameImage(im, ref));
}
TEST(only_grain_leaves_mean_unchanged) {
  Image im = solid(256, 256, 0.5f, 0.5f, 0.5f);
  FilmParams p;
  p.grain.enabled = true;
  process(im, p, {});
  double mean = 0;
  for (int i = 0; i < 256 * 256; ++i) mean += im.px[i * 4];
  mean /= 256 * 256;
  CHECK_NEAR(mean, 0.5, 0.01);
}
TEST(alpha_never_modified_and_transparent_pixels_untouched) {
  Image im = solid(40, 40, 1.5f, 1.5f, 1.5f, 0.5f);
  rect(im, 0, 0, 10, 40, 0.f, 0.f, 0.f);
  for (int y = 0; y < 40; ++y)
    for (int x = 0; x < 10; ++x) im.at(x, y)[3] = 0.f;  // transparent strip
  Image ref = im;
  process(im, allOn(), {});
  for (int y = 0; y < 40; ++y)
    for (int x = 0; x < 40; ++x) {
      CHECK(im.at(x, y)[3] == ref.at(x, y)[3]);
      if (x < 10) CHECK(std::memcmp(im.at(x, y), ref.at(x, y), 16) == 0);
    }
}

// ---------- robustness ----------
TEST(odd_sizes_and_tiny_images_are_safe) {
  for (auto [w, h] : std::vector<std::pair<int, int>>{{1, 1}, {2, 3}, {7, 1}, {1, 9}, {33, 17}, {101, 3}}) {
    Image im = solid(w, h, 2.f, 1.f, 0.5f);
    CHECK(process(im, allOn(), {}));
    CHECK(allFinite(im));
  }
  Image empty;
  CHECK(!process(empty, allOn(), {}));
}
TEST(extreme_values_stay_finite) {
  Image im = solid(48, 32, 0.f, 0.f, 0.f);
  rect(im, 10, 10, 20, 20, 1000.f, 500.f, 10.f);  // HDR spike
  rect(im, 30, 10, 40, 20, -0.2f, -0.5f, 0.1f);   // negative values from grading
  im.at(0, 0)[0] = NAN;                           // corrupt input pixel
  FilmParams p = allOn();
  p.bloom.intensity = p.halation.intensity = p.grain.amount = 1.f;
  p.bloom.radius = 25.f;
  CHECK(process(im, p, {}));
  CHECK(allFinite(im) || std::isnan(im.at(0, 0)[0]));  // only the corrupt pixel may stay NaN
  for (size_t i = 4; i < im.px.size(); ++i) CHECK(std::isfinite(im.px[i]));
}
TEST(invalid_params_are_sanitised) {
  FilmParams p = allOn();
  p.bloom.radius = NAN;
  p.grain.size = -5.f;
  p.mix = 7.f;
  FilmParams s = sanitized(p);
  CHECK(std::isfinite(s.bloom.radius) && s.bloom.radius > 0);
  CHECK(s.grain.size >= 0.5f);
  CHECK(s.mix == 1.f);
}
TEST(no_hdr_clipping) {
  Image im = solid(32, 32, 3.f, 3.f, 3.f);
  FilmParams p;
  p.bloom.enabled = true;
  process(im, p, {});
  CHECK(im.at(16, 16)[0] > 3.f);  // energy added on top, nothing clamped to 1
}

// ---------- soft knee ----------
TEST(soft_knee_is_monotonic_and_continuous) {
  float prevExcess = 0.f;
  for (float l = 0.f; l < 4.f; l += 0.001f) {
    float excess = l * vv_soft_knee_weight(l, 0.8f, 0.5f);
    CHECK(excess >= prevExcess - 1e-6f);
    CHECK(excess - prevExcess < 0.0011f);  // no jumps
    prevExcess = excess;
  }
  CHECK(vv_soft_knee_weight(0.2f, 0.8f, 0.5f) == 0.f);
  CHECK_NEAR(2.f * vv_soft_knee_weight(2.f, 0.8f, 0.5f), 1.2f, 1e-5f);
}
TEST(bloom_increases_monotonically_with_intensity_and_lower_threshold) {
  Image base = solid(64, 64, 0.f, 0.f, 0.f);
  rect(base, 30, 30, 34, 34, 2.f, 2.f, 2.f);
  auto glowAt = [&](float intensity, float threshold) {
    Image im = base;
    FilmParams p;
    p.bloom.enabled = true;
    p.bloom.intensity = intensity;
    p.bloom.threshold = threshold;
    process(im, p, {});
    return im.at(40, 32)[1];
  };
  CHECK(glowAt(0.1f, 0.8f) < glowAt(0.3f, 0.8f));
  CHECK(glowAt(0.3f, 1.5f) < glowAt(0.3f, 0.8f));
}

// ---------- blur ----------
TEST(blur_preserves_energy_and_matches_reference) {
  Plane p(400, 300, 1);  // large enough that 4 sigma never reaches the border
  p.at(160, 120)[0] = 1000.f;
  for (float sigma : {1.f, 3.f, 8.f, 20.f}) {
    Plane b = gaussianBlur(p, sigma);
    double sum = std::accumulate(b.v.begin(), b.v.end(), 0.0);
    CHECK_NEAR(sum, 1000.0, 1000.0 * 0.01);
    if (sigma > 6.f) {  // pyramid path vs exact reference
      Plane ref = gaussianBlurDirect(p, sigma);
      double maxv = *std::max_element(ref.v.begin(), ref.v.end()), err = 0;
      for (size_t i = 0; i < ref.v.size(); ++i) err = std::max(err, (double)std::fabs(ref.v[i] - b.v[i]));
      CHECK(err < 0.12 * maxv);
    }
  }
}
TEST(blur_has_no_dark_border) {
  Plane p(50, 30, 1);
  std::fill(p.v.begin(), p.v.end(), 1.f);
  Plane b = gaussianBlur(p, 12.f);
  for (float v : b.v) CHECK_NEAR(v, 1.f, 1e-4f);
}

// ---------- bloom ----------
TEST(bloom_point_light_spreads_symmetrically_and_falls_off) {
  Image im = solid(201, 201, 0.f, 0.f, 0.f);
  rect(im, 99, 99, 102, 102, 8.f, 8.f, 8.f);
  FilmParams p;
  p.bloom.enabled = true;
  p.space = WorkingSpace::Linear;
  process(im, p, {});
  float r10 = im.at(110, 100)[1], l10 = im.at(90, 100)[1], u10 = im.at(100, 90)[1];
  float r40 = im.at(140, 100)[1];
  CHECK(r10 > 0.f);
  CHECK_NEAR(r10, l10, r10 * 0.03f);
  CHECK_NEAR(r10, u10, r10 * 0.03f);
  CHECK(r40 < r10);
  CHECK(r40 > 0.f);  // multiscale: wide skirt still present
}
TEST(bloom_is_neutral_for_white_light) {
  Image im = solid(80, 80, 0.f, 0.f, 0.f);
  rect(im, 38, 38, 42, 42, 4.f, 4.f, 4.f);
  FilmParams p;
  p.bloom.enabled = true;
  process(im, p, {});
  const float* q = im.at(48, 40);
  CHECK_NEAR(q[0], q[1], 1e-5f);
  CHECK_NEAR(q[1], q[2], 1e-5f);
}
TEST(bloom_uniform_highlight_is_stable_across_frame) {
  Image im = solid(64, 48, 2.f, 2.f, 2.f);
  FilmParams p;
  p.bloom.enabled = true;
  process(im, p, {});
  float c = im.at(32, 24)[0];
  CHECK_NEAR(im.at(0, 0)[0], c, 1e-3f);    // corners equal centre: no dark/bright border
  CHECK_NEAR(im.at(63, 47)[0], c, 1e-3f);
}
TEST(bloom_radius_scales_with_resolution) {
  // Same picture at 1x and 2x resolution: glow at the same relative position is the same.
  auto make = [](int s) {
    Image im = solid(192 * s, 108 * s, 0.f, 0.f, 0.f);
    rect(im, 92 * s, 50 * s, 100 * s, 58 * s, 3.f, 3.f, 3.f);
    FilmParams p;
    p.bloom.enabled = true;  // radius 4% of height: sigma 4.3 px at 1x, 8.6 px at 2x
    process(im, p, {});
    return im.at(106 * s, 54 * s)[1];
  };
  float a = make(1), b = make(2);
  CHECK(a > 0.f);
  CHECK_NEAR(a, b, a * 0.15f);
}

// ---------- halation ----------
TEST(halation_is_warm_and_concentrated_at_edges) {
  // Bright window inside a dark frame.
  // 1080-line frame, default radius (0.6% of height ~ 6.5 px sigma).
  Image im = solid(400, 1080, 0.02f, 0.02f, 0.02f);
  rect(im, 100, 400, 300, 700, 1.0f, 1.0f, 1.0f);
  Image ref = im;
  FilmParams p;
  p.halation.enabled = true;
  p.halation.intensity = 0.5f;
  Layers L;
  process(im, p, {}, &L);
  // spill just outside the edge
  const float* out = L.halationAdd.at(98, 550);  // 2 px outside the bright edge
  CHECK(out[0] > 0.f);
  CHECK(out[0] > 3.f * out[1]);  // red dominant
  CHECK(out[1] >= out[2]);       // warm (orange), not magenta
  // far away from the window: nothing
  CHECK(L.halationAdd.at(20, 100)[0] < out[0] * 0.01f);
  // deep inside the bright area: much less than at the edge
  CHECK(L.halationAdd.at(200, 550)[0] < out[0] * 0.35f);
  // no global red shift: average change over dark corners ~ 0
  CHECK_NEAR(im.at(2, 2)[0], ref.at(2, 2)[0], 1e-6f);
}
TEST(halation_warmth_moves_towards_orange) {
  vv_float3 red = fe_halation_tint(0.f), orange = fe_halation_tint(1.f);
  CHECK(orange.y / orange.x > red.y / red.x);
  CHECK_NEAR(vv_luma(red), 0.3f, 1e-4f);
  CHECK_NEAR(vv_luma(orange), 0.3f, 1e-4f);
}
TEST(halation_responds_to_cyan_less_than_white) {
  auto edgeRed = [](float r, float g, float b) {
    Image im = solid(240, 1080, 0.f, 0.f, 0.f);  // realistic frame height for the default radius
    rect(im, 80, 400, 160, 700, r, g, b);
    FilmParams p;
    p.halation.enabled = true;
    Layers L;
    process(im, p, {}, &L);
    return L.halationAdd.at(77, 550)[0];
  };
  float white = edgeRed(1.5f, 1.5f, 1.5f), cyan = edgeRed(0.f, 1.5f, 1.5f), magenta = edgeRed(1.5f, 0.f, 1.5f);
  CHECK(white > cyan);
  CHECK(white > 0.f);
  CHECK(magenta > cyan);  // red content drives halation
}

// ---------- grain ----------
static Image grainFrame(int64_t key, int seed = 1, int w = 192, int h = 108) {
  Image im = solid(w, h, 0.5f, 0.5f, 0.5f);
  FilmParams p;
  p.grain.enabled = true;
  p.grain.seed = seed;
  process(im, p, RenderContext{key});
  return im;
}
static double corr(const Image& a, const Image& b) {
  double ma = 0, mb = 0, n = a.width * a.height;
  for (int i = 0; i < n; ++i) { ma += a.px[i * 4]; mb += b.px[i * 4]; }
  ma /= n; mb /= n;
  double sab = 0, saa = 0, sbb = 0;
  for (int i = 0; i < n; ++i) {
    double x = a.px[i * 4] - ma, y = b.px[i * 4] - mb;
    sab += x * y; saa += x * x; sbb += y * y;
  }
  return sab / std::sqrt(saa * sbb);
}
static double stddev(const Image& a) {
  double m = 0, s = 0, n = a.width * a.height;
  for (int i = 0; i < n; ++i) m += a.px[i * 4];
  m /= n;
  for (int i = 0; i < n; ++i) s += (a.px[i * 4] - m) * (a.px[i * 4] - m);
  return std::sqrt(s / n);
}
TEST(grain_is_deterministic_for_same_time_and_seed) {
  CHECK(sameImage(grainFrame(42), grainFrame(42)));
}
TEST(grain_render_order_does_not_matter) {
  Image a1 = grainFrame(7), b1 = grainFrame(8), c1 = grainFrame(9);
  Image c2 = grainFrame(9), a2 = grainFrame(7), b2 = grainFrame(8);
  CHECK(sameImage(a1, a2) && sameImage(b1, b2) && sameImage(c1, c2));
}
TEST(grain_changes_per_frame_but_stays_statistically_similar) {
  Image a = grainFrame(100), b = grainFrame(101);
  CHECK(!sameImage(a, b));
  CHECK(std::fabs(corr(a, b)) < 0.1);
  CHECK_NEAR(stddev(a), stddev(b), stddev(a) * 0.1);
}
TEST(grain_seed_changes_pattern) {
  CHECK(std::fabs(corr(grainFrame(5, 1), grainFrame(5, 2))) < 0.1);
}
TEST(grain_static_when_animate_off) {
  auto frame = [](int64_t key) {
    Image im = solid(64, 64, 0.5f, 0.5f, 0.5f);
    FilmParams p;
    p.grain.enabled = true;
    p.grain.animate = false;
    process(im, p, RenderContext{key});
    return im;
  };
  CHECK(sameImage(frame(1), frame(999)));
}
TEST(grain_is_spatially_correlated_not_white_noise) {
  // Realistic frame height (grain size is relative to a 1080-line frame).
  Image a = grainFrame(3, 1, 256, 1080);
  double num = 0, den = 0, m = 0, n = 256.0 * 255;
  for (int y = 0; y < 1080; ++y)
    for (int x = 0; x < 256; ++x) m += a.at(x, y)[0];
  m /= 256.0 * 1080;
  for (int y = 0; y < 1080; ++y)
    for (int x = 0; x < 255; ++x) {
      num += (a.at(x, y)[0] - m) * (a.at(x + 1, y)[0] - m);
      den += (a.at(x, y)[0] - m) * (a.at(x, y)[0] - m);
    }
  (void)n;
  double rho = num / den;
  CHECK(rho > 0.2);   // correlated (white noise would be ~0)
  CHECK(rho < 0.95);  // but not a smooth blur
}
TEST(grain_has_no_periodic_tiling) {
  // Compare the frame with copies of itself shifted by common tile sizes.
  Image a = grainFrame(11, 1, 600, 300);
  for (int shift : {32, 64, 128, 256}) {
    double num = 0, da = 0, db = 0;
    for (int y = 0; y < 300; ++y)
      for (int x = 0; x + shift < 600; ++x) {
        double p = a.at(x, y)[0] - 0.5, q = a.at(x + shift, y)[0] - 0.5;
        num += p * q; da += p * p; db += q * q;
      }
    CHECK(std::fabs(num / std::sqrt(da * db)) < 0.05);
  }
}
TEST(grain_response_follows_luminance_bands) {
  auto sd = [](float level) {
    Image im = solid(200, 200, level, level, level);
    FilmParams p;
    p.grain.enabled = true;
    process(im, p, RenderContext{1});
    return stddev(im);
  };
  double shadow = sd(0.08f), mid = sd(0.5f), high = sd(0.95f);
  CHECK(mid > shadow);
  CHECK(mid > high);
  CHECK(shadow > 0.0);
}
TEST(grain_luma_mode_is_neutral) {
  Image im = grainFrame(4);
  for (int i = 0; i < 1000; ++i) {
    CHECK(im.px[i * 4] == im.px[i * 4 + 1]);
    CHECK(im.px[i * 4 + 1] == im.px[i * 4 + 2]);
  }
}
TEST(grain_pattern_matches_between_preview_and_full_res) {
  // Half-resolution preview samples the same continuous field (grain coordinates are frame-relative).
  GrainParams g;
  g.size = 4.f;
  float full = grainSample(200, 100, 1080, g, 3, 0);
  float half = grainSample(100, 50, 540, g, 3, 0);
  CHECK(std::fabs(full - half) < 0.6f);  // same neighbourhood of the same field
}

// ---------- pixel I/O ----------
TEST(pixel_io_roundtrips_8bit_and_float_with_strides) {
  const int w = 7, h = 5;
  // BGRA 8u with padded, then negative (bottom-up) stride
  for (ptrdiff_t pad : {0, 12}) {
    std::vector<uint8_t> buf((w * 4 + pad) * h);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<uint8_t>(i * 37 + 11);
    for (bool bottomUp : {false, true}) {
      ptrdiff_t stride = w * 4 + pad;
      uint8_t* base = bottomUp ? buf.data() + stride * (h - 1) : buf.data();
      ptrdiff_t s = bottomUp ? -stride : stride;
      Image im;
      readPixels(base, s, w, h, PixelLayout::BGRA_8u, im);
      std::vector<uint8_t> out(buf.size(), 0);
      uint8_t* obase = bottomUp ? out.data() + stride * (h - 1) : out.data();
      writePixels(im, obase, s, PixelLayout::BGRA_8u);
      for (int y = 0; y < h; ++y) CHECK(std::memcmp(&buf[y * stride], &out[y * stride], w * 4) == 0);
    }
  }
  std::vector<float> f(w * h * 4);
  for (size_t i = 0; i < f.size(); ++i) f[i] = (i % 9) * 0.37f - 0.5f;
  Image im;
  readPixels(f.data(), w * 16, w, h, PixelLayout::BGRA_32f, im);
  std::vector<float> o(f.size());
  writePixels(im, o.data(), w * 16, PixelLayout::BGRA_32f);
  CHECK(o == f);
}
TEST(pixel_io_premultiplied_argb) {
  uint8_t px[4] = {128, 64, 32, 16};  // A R G B, premultiplied
  Image im;
  readPixels(px, 4, 1, 1, PixelLayout::ARGB_8u, im);
  CHECK_NEAR(im.px[0], 0.5f, 0.01f);
  uint8_t out[4];
  writePixels(im, out, 4, PixelLayout::ARGB_8u, px, 4);
  CHECK(std::memcmp(px, out, 4) == 0);
}

TEST(pixel_io_ae_16bit_and_float_premultiplied) {
  uint16_t px16[4] = {16384, 8192, 4096, 2048};  // A R G B premultiplied, alpha 0.5
  Image im;
  readPixels(px16, 8, 1, 1, PixelLayout::ARGB_16u, im);
  CHECK_NEAR(im.px[0], 0.5f, 1e-4f);
  CHECK_NEAR(im.px[3], 0.5f, 1e-4f);
  uint16_t o16[4];
  writePixels(im, o16, 8, PixelLayout::ARGB_16u, px16, 8);
  CHECK(std::memcmp(px16, o16, 8) == 0);
  float pf[4] = {0.5f, 0.25f, 0.6f, 0.0f};  // straight R = 0.5, G = 1.2 (super-white)
  readPixels(pf, 16, 1, 1, PixelLayout::ARGB_32f, im);
  CHECK_NEAR(im.px[1], 1.2f, 1e-6f);
  float of[4];
  writePixels(im, of, 16, PixelLayout::ARGB_32f, pf, 16);
  for (int i = 0; i < 4; ++i) CHECK_NEAR(of[i], pf[i], 1e-6f);
}

// ---------- presets ----------
TEST(presets_exist_and_are_sane) {
  for (const char* n : {"Subtle Film", "Soft Highlight Glow", "Night Lights", "Fine 35mm Grain",
                        "Coarse 16mm Grain", "Halation Only", "Off"}) {
    const Preset* p = findPreset(n);
    CHECK(p != nullptr);
    if (p) {
      FilmParams s = sanitized(p->params);
      CHECK(std::memcmp(&s.bloom, &p->params.bloom, sizeof(s.bloom)) == 0);
    }
  }
  CHECK(!findPreset("Off")->params.anyEffectActive());
  CHECK(findPreset("Halation Only")->params.halation.enabled);
  CHECK(!findPreset("Halation Only")->params.bloom.enabled);
}

// ---------- host parameter mapping (no SDK needed) ----------
// Reader/writer backed by an array indexed like the host's params (simulates a host).
struct FakeHost : ParamReader, vv::ParamWriter {
  std::vector<double> vals;
  FakeHost() {
    const auto& all = allParams();
    vals.assign(all.size() + 1, 0.0);
    for (size_t i = 0; i < all.size(); ++i) vals[i + 1] = all[i].dflt;  // host starts at defaults
  }
  void set(ParamKey k, double x) { vals[paramIndex(k)] = x; }
  double get(ParamKey k) const { return vals[paramIndex(k)]; }
  double number(int i) const override { return vals[i]; }
  bool flag(int i) const override { return vals[i] != 0.0; }
  int popup(int i) const override { return static_cast<int>(vals[i]); }
  void setNumber(int i, double v) override { vals[i] = v; }
  void setFlag(int i, bool v) override { vals[i] = v ? 1.0 : 0.0; }
  void setPopup(int i, int v) override { vals[i] = v; }
};

TEST(param_table_is_complete_and_stable) {
  const auto& all = allParams();
  CHECK(all.size() == static_cast<size_t>(ParamKey::Count));
  std::vector<int> ids;
  for (int k = 0; k < static_cast<int>(ParamKey::Count); ++k) {
    const ParamSpec& sp = specFor(static_cast<ParamKey>(k));
    CHECK(sp.key == k);  // table order == enum order
    CHECK(paramIndex(static_cast<ParamKey>(k)) == k + 1);
    ids.push_back(sp.diskId);
    if (sp.kind == ParamKind::Float || sp.kind == ParamKind::IntSlider) {
      CHECK(sp.validMin <= sp.sliderMin && sp.sliderMin < sp.sliderMax && sp.sliderMax <= sp.validMax);
      CHECK(sp.dflt >= sp.validMin && sp.dflt <= sp.validMax);
    }
    if (sp.kind == ParamKind::Popup) CHECK(sp.items && sp.itemCount > 1 && sp.dflt >= 1 && sp.dflt <= sp.itemCount);
  }
  std::sort(ids.begin(), ids.end());
  CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());  // disk IDs unique
  // On-disk IDs never change (projects depend on them).
  CHECK(specFor(ParamKey::Preset).diskId == 1);
  CHECK(specFor(ParamKey::BloomRadius).diskId == 11);
  CHECK(specFor(ParamKey::GrainGroupEnd).diskId == 32);
  CHECK(specFor(ParamKey::GateGroupEnd).diskId == 44);
  int depth = 0;
  for (const ParamSpec& sp : all) {
    if (sp.kind == ParamKind::GroupStart) ++depth;
    if (sp.kind == ParamKind::GroupEnd) --depth;
    CHECK(depth >= 0 && depth <= 1);
  }
  CHECK(depth == 0);
}

TEST(host_param_mapping) {
  FakeHost r;
  r.set(ParamKey::Mix, 50.0);
  r.set(ParamKey::WorkingSpace, 3);
  r.set(ParamKey::Quality, 2);
  r.set(ParamKey::GrainEnable, 1);
  r.set(ParamKey::GrainAmount, 0.3);
  r.set(ParamKey::GrainSeed, 77);
  r.set(ParamKey::GrainColor, 2);
  r.set(ParamKey::BloomRadius, 1e9);  // out of range -> clamped
  FilmParams p = readParams(r);
  CHECK(p.masterEnable);
  CHECK_NEAR(p.mix, 0.5f, 1e-6f);
  CHECK(p.space == WorkingSpace::Linear);
  CHECK(p.quality == Quality::High);
  CHECK(p.grain.enabled && p.grain.seed == 77 && p.grain.color == GrainColor::Color);
  CHECK(!p.bloom.enabled && !p.halation.enabled && !p.gate.enabled);  // table defaults
  CHECK(p.bloom.radius <= 25.f);
  // Defaults of the host table equal the core defaults
  FilmParams d = readParams(FakeHost());
  FilmParams core;
  CHECK(d.bloom.intensity == core.bloom.intensity && d.grain.size == core.grain.size && d.mix == core.mix &&
        d.gate.weave == core.gate.weave && d.gate.seed == core.gate.seed);
}

TEST(effect_interface_presets_bypass_and_render) {
  const vv::Effect& fx = vv::effect();
  CHECK(std::string(fx.info().matchName) == "VVIS FilmEffects");
  CHECK(std::string(fx.info().ofxId) == "com.venostavisuals.FilmEffects");
  CHECK(std::string(fx.info().ofxParamPrefix) == "fe");
  CHECK(&fx.params() == &allParams());
  FakeHost h;
  CHECK(!fx.active(h));  // everything off by default -> host bypass
  // Choosing "Old Projector" in the menu fills in the controls and resets the menu.
  const int menu = paramIndex(ParamKey::Preset);
  h.setPopup(menu, FE_PRESET_COUNT);
  CHECK(fx.paramChanged(menu, h, h));
  CHECK(h.get(ParamKey::Preset) == 1);
  CHECK(h.get(ParamKey::GateEnable) == 1 && h.get(ParamKey::GrainEnable) == 1);
  CHECK_NEAR(h.get(ParamKey::GateWeave), 1.6, 1e-6);
  CHECK(!fx.paramChanged(paramIndex(ParamKey::Mix), h, h));
  CHECK(fx.active(h));
  // Rendering through the interface equals the core pipeline.
  Image a = solid(120, 70, 0.4f, 0.45f, 0.5f), b = a;
  CHECK(fx.render(a, h, 6));
  CHECK(process(b, readParams(h), RenderContext{6}));
  CHECK(sameImage(a, b));
}

TEST(frame_keys_from_host_time) {
  // 23.976 in Premiere ticks: 10594584000 per frame
  const int64_t tpf = 10594584000LL;
  CHECK(frameKeyFromTime(0, tpf) == 0);
  CHECK(frameKeyFromTime(5 * tpf, tpf) == 5);
  CHECK(frameKeyFromTime(5 * tpf + 1, tpf) != 5);  // sub-frame time does not collide
  CHECK(frameKeyFromTime(5 * tpf + 1, tpf) != frameKeyFromTime(5 * tpf + 2, tpf));
  // AE-style units: current_time / time_step in time_scale units (e.g. 1001/24000)
  CHECK(frameKeyFromTime(1001 * 9, 1001) == 9);
}

// ---------- sizes / timing smoke ----------
TEST(hd_and_uhd_frames_process) {
  for (auto [w, h] : std::vector<std::pair<int, int>>{{1920, 1080}, {3840, 2160}}) {
    Image im = solid(w, h, 0.4f, 0.4f, 0.4f);
    rect(im, w / 2, h / 2, w / 2 + w / 20, h / 2 + h / 20, 3.f, 2.5f, 2.f);
    auto t0 = std::chrono::steady_clock::now();
    CHECK(process(im, allOn(), {}));
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  [info] %dx%d all effects, CPU reference, 1 run: %.0f ms\n", w, h, ms);
    CHECK(allFinite(im));
  }
}


// ---------- Film Gate ----------
static FilmParams gateOnly() {
  FilmParams p;
  p.gate.enabled = true;
  p.gate.weave = 0.f; p.gate.flicker = 0.f; p.gate.edge = 0.f; p.gate.dust = 0.f; p.gate.scratches = 0.f;
  return p;
}
TEST(gate_enabled_with_all_amounts_zero_is_bit_exact) {
  Image im = solid(64, 48, 0.4f, 0.5f, 0.6f), ref = im;
  CHECK(!gateOnly().anyEffectActive());
  CHECK(process(im, gateOnly(), {}));
  CHECK(sameImage(im, ref));
}
TEST(gate_weave_by_whole_pixels_is_an_exact_shift) {
  Image film(40, 30);
  for (int y = 0; y < 30; ++y)
    for (int x = 0; x < 40; ++x) {
      float* p = film.at(x, y);
      p[0] = x * 0.01f; p[1] = y * 0.02f; p[2] = (x * 7 + y * 3) % 11 * 0.05f; p[3] = 1.f;
    }
  GateFrame f;
  f.active = f.doWeave = true;
  f.dx = 2.f; f.dy = 1.f;
  Image out = film;
  applyGate(film, film, out, f, 0, 1.f);
  bool exact = true;
  for (int y = 2; y < 28; ++y)
    for (int x = 3; x < 37; ++x)
      for (int c = 0; c < 3; ++c) exact = exact && out.at(x, y)[c] == film.at(x - 2, y - 1)[c];
  CHECK(exact);
}
TEST(gate_weave_is_deterministic_bounded_and_mostly_smooth) {
  GateParams g; g.enabled = true; g.weave = 2.f; g.flicker = 0.f; g.dust = 0.f;
  const float amp = 2.f * 2160.f / 1080.f;
  double sumX = 0, sumY = 0, maxStep = 0, meanStep = 0;
  float prevY = 0;
  int distinct = 0;
  for (int t = 0; t < 600; ++t) {
    GateFrame a = gateFrame(g, 3840, 2160, t), b = gateFrame(g, 3840, 2160, t);
    CHECK(a.dx == b.dx && a.dy == b.dy);
    CHECK(std::fabs(a.dx) <= 0.6f * amp + 1e-4f && std::fabs(a.dy) <= amp + 1e-4f);
    sumX += a.dx; sumY += a.dy;
    if (t > 0) {
      double step = std::fabs(a.dy - prevY);
      maxStep = std::max(maxStep, step);
      meanStep += step;
      distinct += a.dy != prevY;
    }
    prevY = a.dy;
  }
  meanStep /= 599;
  CHECK(distinct > 590);                  // moves every frame
  CHECK(std::fabs(sumX / 600) < 0.25 * amp && std::fabs(sumY / 600) < 0.25 * amp);  // centred
  CHECK(meanStep < 0.45 * amp);            // drift dominates over jitter
  CHECK(maxStep < 1.2 * amp);
  // same weave relative to frame height at another resolution
  GateFrame hd = gateFrame(g, 1920, 1080, 77), uhd = gateFrame(g, 3840, 2160, 77);
  CHECK_NEAR(uhd.dy, 2.f * hd.dy, 1e-4f);
}
TEST(gate_flicker_averages_to_one_and_stays_in_range) {
  GateParams g; g.enabled = true; g.weave = 0.f; g.dust = 0.f; g.flicker = 1.f;
  double sum = 0; float lo = 9, hi = 0;
  for (int t = 0; t < 2000; ++t) {
    float gain = gateFrame(g, 1920, 1080, t).gain;
    sum += gain; lo = std::min(lo, gain); hi = std::max(hi, gain);
  }
  CHECK(std::fabs(sum / 2000 - 1.0) < 0.02);
  CHECK(lo >= 0.79f && hi <= 1.21f);
  CHECK(hi - lo > 0.15f);
}
TEST(gate_edge_masks_border_not_centre) {
  CHECK(fe_gate_mask(960, 540, 1920, 1080, 30, 5, 40) == 1.f);
  CHECK(fe_gate_mask(0, 0, 1920, 1080, 30, 5, 40) == 0.f);
  CHECK(fe_gate_mask(10, 540, 1920, 1080, 30, 5, 40) == 0.f);
  float a = fe_gate_mask(24, 540, 1920, 1080, 30, 5, 40), b = fe_gate_mask(30, 540, 1920, 1080, 30, 5, 40),
        c = fe_gate_mask(36, 540, 1920, 1080, 30, 5, 40);
  CHECK(a < b && b < c);  // soft, monotonic ramp
  FilmParams p = gateOnly(); p.gate.edge = 3.f;
  Image im = solid(320, 180, 0.5f, 0.5f, 0.5f);
  CHECK(process(im, p, {}));
  CHECK(im.at(0, 0)[0] == 0.f && im.at(160, 90)[0] == 0.5f);
  CHECK(im.at(0, 0)[3] == 1.f);  // alpha untouched
}
static void dustStats(DefectLook look, int& darker, int& brighter, int n) {
  FilmParams p = gateOnly(); p.gate.dust = 1.f; p.gate.look = look;
  darker = brighter = 0;
  for (int t = 0; t < n; ++t) {
    Image im = solid(480, 270, 0.4f, 0.4f, 0.4f);
    CHECK(process(im, p, RenderContext{t}));
    for (int i = 0; i < 480 * 270; ++i) {
      darker += im.px[i * 4] < 0.4f - 1e-4f;
      brighter += im.px[i * 4] > 0.4f + 1e-4f;
    }
  }
}
TEST(gate_dust_dark_light_and_mixed) {
  int d, b;
  dustStats(DefectLook::Dark, d, b, 4);
  CHECK(d > 0 && b == 0);
  const double frac = d / (4.0 * 480 * 270);
  CHECK(frac > 0.0005 && frac < 0.05);  // specks, not a veil
  dustStats(DefectLook::Light, d, b, 4);
  CHECK(b > 0 && d == 0);
  dustStats(DefectLook::Mixed, d, b, 4);
  CHECK(b > 0 && d > 0);
}
TEST(gate_dust_changes_every_frame_and_is_reproducible) {
  FilmParams p = gateOnly(); p.gate.dust = 0.6f;
  Image a = solid(320, 180, 0.5f, 0.5f, 0.5f), b = a, c = a;
  process(a, p, RenderContext{10}); process(b, p, RenderContext{10}); process(c, p, RenderContext{11});
  CHECK(sameImage(a, b));
  CHECK(!sameImage(a, c));
}
TEST(gate_scratches_are_vertical_lines_that_persist) {
  GateParams g; g.enabled = true; g.weave = 0.f; g.flicker = 0.f; g.dust = 0.f; g.scratches = 1.f;
  int framesWith = 0, persisted = 0;
  for (int t = 0; t < 200; ++t) {
    GateFrame f = gateFrame(g, 1920, 1080, t), n = gateFrame(g, 1920, 1080, t + 1);
    if (f.scratchCount > 0) ++framesWith;
    if (f.scratchCount > 0 && n.scratchCount > 0 && std::fabs(f.scratches[0].x0 - n.scratches[0].x0) < 5.f) ++persisted;
  }
  CHECK(framesWith > 100);
  CHECK(persisted > framesWith / 2);
  // Darkening concentrated in a few columns.
  FilmParams p = gateOnly(); p.gate.scratches = 1.f;
  int t = 0;
  while (gateFrame(p.gate, 400, 300, t).scratchCount == 0) ++t;
  Image im = solid(400, 300, 0.5f, 0.5f, 0.5f);
  process(im, p, RenderContext{t});
  int columns = 0;
  for (int x = 0; x < 400; ++x) {
    int hit = 0;
    for (int y = 0; y < 300; ++y) hit += im.at(x, y)[0] < 0.499f;
    columns += hit > 60;
  }
  CHECK(columns >= 1 && columns < 40);
}
TEST(gate_respects_mix_alpha_and_transparency) {
  FilmParams p = gateOnly(); p.gate.edge = 5.f; p.mix = 0.5f;
  Image im = solid(200, 100, 0.5f, 0.5f, 0.5f, 0.7f);
  for (int x = 0; x < 200; ++x) im.at(x, 50)[3] = 0.f;
  Image ref = im;
  process(im, p, {});
  CHECK_NEAR(im.at(0, 0)[0], 0.25f, 1e-6f);  // half way to black
  CHECK(im.at(0, 0)[3] == 0.7f);
  CHECK(std::memcmp(im.at(0, 50), ref.at(0, 50), 16) == 0);
}
TEST(gate_old_projector_preset_is_finite_everywhere) {
  const Preset* pr = findPreset("Old Projector");
  CHECK(pr && pr->params.gate.anyActive());
  for (int t = 0; t < 5; ++t) {
    Image im = solid(257, 131, 0.6f, 0.5f, 0.4f);
    rect(im, 100, 40, 140, 80, 4.f, 4.f, 4.f);
    CHECK(process(im, pr->params, RenderContext{t}));
    CHECK(allFinite(im));
  }
  CHECK(std::string(presetMenuName(FE_PRESET_COUNT)) == "Old Projector");
}
TEST(gate_preset_values_cover_every_gate_control) {
  int gateKeys = 0;
  forEachPresetValue(FilmParams{}, [&](ParamKey k, double) {
    gateKeys += static_cast<int>(k) >= static_cast<int>(ParamKey::GateEnable) && static_cast<int>(k) <= static_cast<int>(ParamKey::GateLook);
  });
  CHECK(gateKeys == 9);
}

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  for (auto& [name, fn] : registry()) {
    if (filter && name.find(filter) == std::string::npos) continue;
    int before = g_fail;
    fn();
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name.c_str());
  }
  std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
