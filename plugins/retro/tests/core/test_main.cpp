// Retro tests (no host, no GPU): kernel maths, CPU reference, parameter table, effect interface.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "RtParamsTable.h"
#include "rt.h"
#include "vv/effect.h"
#include "vv/scenes.h"

using namespace rt;

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

static Image solid(int w, int h, float r, float g, float b, float a = 1.f) {
  Image im(w, h);
  for (int i = 0; i < w * h; ++i) {
    im.px[i * 4] = r; im.px[i * 4 + 1] = g; im.px[i * 4 + 2] = b; im.px[i * 4 + 3] = a;
  }
  return im;
}
static Params none() {
  Params p;
  p.bleed = p.tape = p.noise = p.crt = false;
  return p;
}
static double maxDiff(const Image& a, const Image& b) {
  double m = 0;
  for (size_t i = 0; i < a.px.size(); ++i) m = std::fmax(m, std::fabs(double(a.px[i]) - b.px[i]));
  return m;
}

TEST(yuv_round_trip) {
  for (float r : {0.f, 0.3f, 1.f, 1.7f, -0.1f})
    for (float g : {0.f, 0.5f, 1.f})
      for (float b : {0.f, 0.8f, 1.f}) {
        const vv_float3 c = rt_yuv_to_rgb(rt_rgb_to_yuv(vv_make3(r, g, b)));
        CHECK(std::fabs(c.x - r) < 1e-5f && std::fabs(c.y - g) < 1e-5f && std::fabs(c.z - b) < 1e-5f);
      }
  const vv_float3 grey = rt_rgb_to_yuv(vv_make3(0.4f, 0.4f, 0.4f));
  CHECK(std::fabs(grey.x - 0.4f) < 1e-6f && std::fabs(grey.y) < 1e-6f && std::fabs(grey.z) < 1e-6f);
}

TEST(bypass_is_bit_exact) {
  const Image src = vv::scenes::makeScene("chart", 64, 36);
  Params off;
  off.masterEnable = false;
  Params zero;
  zero.mix = 0.f;
  for (const Params& p : {off, zero, none()}) {
    Image im = src;
    CHECK(!p.anyActive() || p.mix == 0.f);
    CHECK(process(im, p, 5));
    CHECK(im.px == src.px);
  }
  Image im = src;
  CHECK(process(im, findPreset("Off")->params, 5) && im.px == src.px);
}

TEST(neutral_bleed_settings_are_identity) {
  Params p = none();
  p.bleed = true;
  p.lumaSoft = 0.f; p.ringing = 0.f; p.chromaBlur = 0.f; p.chromaShift = 0.f; p.saturation = 1.f;
  const Image src = vv::scenes::makeScene("chart", 80, 45);
  Image im = src;
  process(im, p, 0);
  CHECK(maxDiff(im, src) < 1e-5);
}

TEST(chroma_bleeds_horizontally_not_vertically) {
  Params p = none();
  p.bleed = true;
  p.chromaShift = 0.f;
  p.ringing = 0.f;
  p.chromaBlur = 20.f;  // sigma 4 px on a 216-line frame
  // Left half red, right half blue: colour spreads across the vertical edge.
  Image v(200, 216);
  for (int y = 0; y < 216; ++y)
    for (int x = 0; x < 200; ++x) {
      float* q = v.at(x, y);
      q[0] = x < 100 ? 0.8f : 0.1f; q[1] = 0.2f; q[2] = x < 100 ? 0.1f : 0.8f; q[3] = 1.f;
    }
  Image vr = v;
  process(vr, p, 0);
  CHECK(vr.at(103, 100)[0] > 0.15f);   // red reaches into the blue side
  CHECK(std::fabs(vr.at(5, 100)[0] - 0.8f) < 0.15f);
  // Top half red, bottom half blue: no vertical spread at all.
  Image h(40, 200);
  for (int y = 0; y < 200; ++y)
    for (int x = 0; x < 40; ++x) {
      float* q = h.at(x, y);
      q[0] = y < 100 ? 0.8f : 0.1f; q[1] = 0.2f; q[2] = y < 100 ? 0.1f : 0.8f; q[3] = 1.f;
    }
  Image hr = h;
  p.saturation = 1.f;
  process(hr, p, 0);
  for (int x : {0, 20, 39}) {
    CHECK(std::fabs(hr.at(x, 99)[0] - 0.8f) < 1e-4f);
    CHECK(std::fabs(hr.at(x, 100)[0] - 0.1f) < 1e-4f);
  }
}

TEST(chroma_shift_moves_colour_right) {
  Params p = none();
  p.bleed = true;
  p.lumaSoft = 0.f; p.ringing = 0.f; p.chromaBlur = 0.f; p.saturation = 1.f;
  p.chromaShift = 4.f;  // px @1080, frame is 1080 tall
  Image im = solid(64, 1080, 0.3f, 0.3f, 0.3f);
  for (int y = 0; y < 1080; ++y) im.at(30, y)[0] = 0.9f;  // red line at x = 30
  Image o = im;
  process(o, p, 0);
  // Chroma sampled 4 px to the right: the red colour appears at x = 26.
  CHECK(o.at(26, 500)[0] > o.at(30, 500)[0]);
}

TEST(ringing_overshoots_at_edges) {
  Params p = none();
  p.bleed = true;
  p.chromaBlur = 0.f; p.chromaShift = 0.f; p.saturation = 1.f; p.lumaSoft = 1.f; p.ringing = 1.f;
  Image im(200, 1080);
  for (int y = 0; y < 1080; ++y)
    for (int x = 0; x < 200; ++x) {
      const float v = x < 100 ? 0.2f : 0.7f;
      float* q = im.at(x, y);
      q[0] = q[1] = q[2] = v; q[3] = 1.f;
    }
  process(im, p, 0);
  float hi = 0.f, lo = 1.f;
  for (int x = 90; x < 110; ++x) { hi = std::max(hi, im.at(x, 50)[1]); lo = std::min(lo, im.at(x, 50)[1]); }
  CHECK(hi > 0.7f + 1e-3f && lo < 0.2f - 1e-3f);  // over- and undershoot
  CHECK(std::fabs(im.at(10, 50)[1] - 0.2f) < 1e-4f && std::fabs(im.at(190, 50)[1] - 0.7f) < 1e-4f);
}

TEST(deterministic_per_frame_and_animated) {
  Params p;  // defaults: bleed + tape + noise
  const Image src = vv::scenes::makeScene("night-street", 160, 90);
  Image a = src, b = src, c = src;
  process(a, p, 1234);
  process(b, p, 1234);
  process(c, p, 1235);
  CHECK(a.px == b.px);
  CHECK(a.px != c.px);
  p.animate = false;
  Image d = src, e = src;
  process(d, p, 1);
  process(e, p, 999999);
  CHECK(d.px == e.px);
  p.seed = 4;
  Image f = src;
  process(f, p, 1);
  CHECK(f.px != d.px);
}

TEST(noise_is_zero_mean_and_visible) {
  Params p = none();
  p.noise = true;
  p.lumaNoise = 0.5f;
  p.chromaNoise = 0.5f;
  Image im = solid(256, 256, 0.5f, 0.5f, 0.5f);
  process(im, p, 7);
  double sum = 0, sq = 0;
  for (int i = 0; i < 256 * 256; ++i) {
    const double g = im.px[i * 4 + 1] - 0.5;
    sum += g; sq += g * g;
  }
  const double mean = sum / (256 * 256), sd = std::sqrt(sq / (256 * 256) - mean * mean);
  CHECK(std::fabs(mean) < 0.005);
  CHECK(sd > 0.01 && sd < 0.1);
}

TEST(tape_jitter_moves_lines_and_head_switching_bends_bottom) {
  Params p = none();
  p.tape = true;
  p.jitter = 4.f; p.tracking = 0.f; p.dropouts = 0.f; p.headSwitch = 1.f;
  Image im(400, 216);
  for (int y = 0; y < 216; ++y)
    for (int x = 0; x < 400; ++x) {
      const float v = x < 200 ? 0.f : 1.f;
      float* q = im.at(x, y);
      q[0] = q[1] = q[2] = v; q[3] = 1.f;
    }
  process(im, p, 3);
  auto edge = [&](int y) {
    for (int x = 1; x < 400; ++x)
      if (im.at(x, y)[1] >= 0.5f) return x;
    return 400;
  };
  int lo = 400, hi = 0;
  for (int y = 0; y < 200; ++y) { lo = std::min(lo, edge(y)); hi = std::max(hi, edge(y)); }
  CHECK(hi > lo);                   // lines are displaced differently
  CHECK(hi - lo < 12);              // ... by a few pixels only
  CHECK(std::abs(edge(215) - 200) > 2);  // bottom line bent by head switching
}

TEST(crt_scanlines_mask_and_curvature) {
  Params p = none();
  p.crt = true;
  p.maskType = 0;
  p.curvature = 0.f;
  p.scanlines = 1.f;
  p.lineCount = 108.f;  // 10 px per line on a 1080-tall frame
  Image im = solid(64, 1080, 0.8f, 0.8f, 0.8f);
  process(im, p, 0);
  float lo = 1.f, hi = 0.f;
  for (int y = 500; y < 520; ++y) { lo = std::min(lo, im.at(5, y)[1]); hi = std::max(hi, im.at(5, y)[1]); }
  CHECK(hi > 0.7f && lo < 0.1f);
  // Lines finer than a pixel: an even darkening, not moire.
  p.lineCount = 1080.f * 1.37f;
  p.lineCount = std::min(p.lineCount, 1080.f);
  Image fine = solid(64, 540, 0.8f, 0.8f, 0.8f);
  process(fine, p, 0);
  float flo = 1.f, fhi = 0.f;
  for (int y = 100; y < 300; ++y) { flo = std::min(flo, fine.at(5, y)[1]); fhi = std::max(fhi, fine.at(5, y)[1]); }
  CHECK(fhi - flo < 0.02f);
  CHECK(std::fabs(fine.at(5, 200)[1] - 0.4f) < 0.02f);  // half the light on average
  // Aperture grille keeps the average brightness of a grey field.
  Params m = none();
  m.crt = true; m.scanlines = 0.f; m.curvature = 0.f; m.maskType = 1; m.maskStrength = 0.6f;
  Image g = solid(300, 100, 0.5f, 0.5f, 0.5f);
  process(g, m, 0);
  double avg = 0;
  for (int x = 0; x < 300; ++x) avg += g.at(x, 50)[0];
  CHECK(std::fabs(avg / 300 - 0.5) < 1e-3);
  CHECK(g.at(0, 50)[0] > g.at(1, 50)[0]);  // red stripe then green stripe
  // Curvature: dark corners, centre unchanged.
  m.maskType = 0; m.curvature = 0.5f;
  Image c = solid(192, 108, 0.6f, 0.6f, 0.6f);
  process(c, m, 0);
  CHECK(c.at(0, 0)[1] < 0.01f);
  CHECK(std::fabs(c.at(96, 54)[1] - 0.6f) < 1e-3f);
}

TEST(resolution_independence) {
  // Same look at 1080 and 540 lines: compare a downsampled 1080 render with the 540 render.
  Params p = none();
  p.bleed = true;
  Image big(384, 216), small(192, 108);
  for (Image* im : {&big, &small})
    for (int y = 0; y < im->height; ++y)
      for (int x = 0; x < im->width; ++x) {
        const float u = (x + 0.5f) / im->width;
        float* q = im->at(x, y);
        q[0] = u < 0.5f ? 0.9f : 0.1f; q[1] = 0.3f; q[2] = u < 0.5f ? 0.1f : 0.9f; q[3] = 1.f;
      }
  process(big, p, 0);
  process(small, p, 0);
  double m = 0;
  for (int x = 0; x < 192; ++x) {
    const double b = 0.5 * (big.at(2 * x, 100)[0] + big.at(2 * x + 1, 100)[0]);
    m = std::fmax(m, std::fabs(b - small.at(x, 50)[0]));
  }
  CHECK(m < 0.06);
}

TEST(alpha_transparency_extremes_and_finiteness) {
  Params p;
  p.crt = true;
  p.jitter = 10.f; p.tracking = 1.f; p.dropouts = 1.f; p.headSwitch = 1.f; p.lumaNoise = 1.f; p.chromaNoise = 1.f;
  p.chromaBlur = 30.f; p.chromaShift = -10.f; p.lumaSoft = 6.f; p.ringing = 1.f; p.saturation = 2.f;
  p.maskType = 2; p.maskStrength = 1.f; p.curvature = 1.f; p.mix = 0.7f;
  for (auto [w, h] : std::vector<std::pair<int, int>>{{1, 1}, {3, 5}, {257, 129}}) {
    Image im = solid(w, h, 0.3f, 1.4f, -0.05f, 0.6f), ref = im;
    im.at(0, 0)[3] = 0.f;
    ref.at(0, 0)[3] = 0.f;
    CHECK(process(im, p, 42));
    CHECK(std::memcmp(im.at(0, 0), ref.at(0, 0), 16) == 0);
    for (int i = 0; i < w * h; ++i) {
      CHECK(im.px[i * 4 + 3] == ref.px[i * 4 + 3]);
      for (int c = 0; c < 3; ++c) CHECK(std::isfinite(im.px[i * 4 + c]));
    }
  }
  Params bad;
  bad.lumaSoft = NAN; bad.chromaBlur = INFINITY; bad.mix = -3.f; bad.maskType = 9; bad.lineCount = 0.f;
  const Params s = sanitized(bad);
  CHECK(s.lumaSoft == Params().lumaSoft && s.chromaBlur == Params().chromaBlur && s.mix == 0.f && s.maskType == 1 && s.lineCount == 60.f);
}

TEST(mix_blends_linearly) {
  Params p;
  const Image src = vv::scenes::makeScene("foliage", 96, 54);
  Image full = src, half = src;
  process(full, p, 9);
  p.mix = 0.5f;
  process(half, p, 9);
  double m = 0;
  for (size_t i = 0; i < src.px.size(); ++i)
    if (i % 4 != 3) m = std::fmax(m, std::fabs(half.px[i] - (src.px[i] + 0.5 * (full.px[i] - src.px[i]))));
  CHECK(m < 1e-5);
}

struct FakeHost : vv::ParamReader, vv::ParamWriter {
  std::vector<double> vals;
  FakeHost() {
    const auto& all = allParams();
    vals.assign(all.size() + 1, 0.0);
    for (size_t i = 0; i < all.size(); ++i) vals[i + 1] = all[i].dflt;
  }
  double get(ParamKey k) const { return vals[paramIndex(k)]; }
  double number(int i) const override { return vals[i]; }
  bool flag(int i) const override { return vals[i] != 0.0; }
  int popup(int i) const override { return static_cast<int>(vals[i]); }
  void setNumber(int i, double v) override { vals[i] = v; }
  void setFlag(int i, bool v) override { vals[i] = v ? 1.0 : 0.0; }
  void setPopup(int i, int v) override { vals[i] = v; }
};

TEST(param_table_presets_and_effect_interface) {
  const auto& all = allParams();
  CHECK(all.size() == static_cast<size_t>(ParamKey::Count));
  std::vector<int> ids;
  for (int k = 0; k < static_cast<int>(ParamKey::Count); ++k) {
    CHECK(all[static_cast<size_t>(k)].key == k);
    ids.push_back(all[static_cast<size_t>(k)].diskId);
  }
  std::sort(ids.begin(), ids.end());
  CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
  const vv::Effect& fx = vv::effect();
  CHECK(std::string(fx.info().ofxParamPrefix) == "rt");
  CHECK(std::string(fx.info().matchName) == "VVIS Retro");
  FakeHost h;
  const Params d = readParams(h), core;
  CHECK(d.bleed == core.bleed && d.crt == core.crt && d.maskType == core.maskType && d.seed == core.seed &&
        std::fabs(d.chromaBlur - core.chromaBlur) < 1e-6f && std::fabs(d.ringing - core.ringing) < 1e-6f && fx.active(h));
  // Every preset applied through the panel reads back as that preset.
  for (int item = 2; item <= RT_PRESET_COUNT; ++item) {
    const int menu = paramIndex(ParamKey::Preset);
    h.setPopup(menu, item);
    CHECK(fx.paramChanged(menu, h, h));
    CHECK(h.get(ParamKey::Preset) == 1);
    const Params want = sanitized(findPreset(presetMenuName(item))->params), got = readParams(h);
    CHECK(got.bleed == want.bleed && got.tape == want.tape && got.noise == want.noise && got.crt == want.crt &&
          got.maskType == want.maskType && std::fabs(got.chromaShift - want.chromaShift) < 1e-5f &&
          std::fabs(got.dropouts - want.dropouts) < 1e-5f && std::fabs(got.saturation - want.saturation) < 1e-5f);
  }
  h.setPopup(paramIndex(ParamKey::Preset), 3);  // Worn Tape
  fx.paramChanged(paramIndex(ParamKey::Preset), h, h);
  Image a = vv::scenes::makeScene("night-street", 96, 54), b = a;
  CHECK(fx.render(a, h, 77) && process(b, readParams(h), 77) && a.px == b.px);
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
