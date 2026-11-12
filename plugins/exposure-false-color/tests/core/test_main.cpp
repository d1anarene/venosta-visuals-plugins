// Exposure & False Color tests (no host, no GPU): kernel maths, CPU reference, GPU pass emulation.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "EfParamsTable.h"
#include "ef.h"
#include "ef_gpu_pixels.h"
#include "vv/effect.h"
#include "vv/scenes.h"

using namespace ef;

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
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))
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

static Image solid(int w, int h, float v, float a = 1.f) {
  Image im(w, h);
  for (int i = 0; i < w * h; ++i) {
    im.px[i * 4] = im.px[i * 4 + 1] = im.px[i * 4 + 2] = v;
    im.px[i * 4 + 3] = a;
  }
  return im;
}
static float enc(float linear) { return vv_from_linear1(linear, VV_SPACE_REC709_G24); }
static bool sameRGB(const float* a, float r, float g, float b) { return std::fabs(a[0] - r) < 1e-5f && std::fabs(a[1] - g) < 1e-5f && std::fabs(a[2] - b) < 1e-5f; }
static Params only() {
  Params p;
  p.falseColor = false;
  p.legend = false;
  return p;
}

TEST(bypass_is_bit_exact) {
  Image im = solid(30, 20, 0.4f), ref = im;
  Params p = only();
  CHECK(!p.anyActive());
  CHECK(process(im, p) && im.px == ref.px);
  p.falseColor = true;
  p.masterEnable = false;
  CHECK(process(im, p) && im.px == ref.px);
  p.masterEnable = true;
  p.opacity = 0.f;
  CHECK(process(im, p) && im.px == ref.px);
}

TEST(false_color_bands_in_stops) {
  Params p = only();
  p.falseColor = true;
  auto colorAt = [&](float linear) {
    Image im = solid(8, 8, enc(linear));
    process(im, p);
    return std::vector<float>(im.at(3, 3), im.at(3, 3) + 3);
  };
  auto is = [](const std::vector<float>& c, float r, float g, float b) { return sameRGB(c.data(), r, g, b); };
  CHECK(is(colorAt(0.18f), 0.10f, 0.75f, 0.20f));                   // middle grey: green
  CHECK(is(colorAt(0.18f * 1.15f), 0.10f, 0.75f, 0.20f));           // +0.2 stop: still green
  CHECK(is(colorAt(0.18f * std::pow(2.f, 1.1f)), 0.95f, 0.45f, 0.70f));  // +1.1 stop: skin pink
  CHECK(is(colorAt(1.0f), 0.90f, 0.05f, 0.05f));                     // diffuse white: red
  CHECK(is(colorAt(3.0f), 0.90f, 0.05f, 0.05f));                     // HDR: red
  CHECK(is(colorAt(0.18f * std::pow(2.f, -7.f)), 0.45f, 0.05f, 0.55f));  // crushed: purple
  CHECK(is(colorAt(0.18f * std::pow(2.f, -5.f)), 0.05f, 0.20f, 0.85f));  // deep shadows: blue
  std::vector<float> g = colorAt(0.18f * std::pow(2.f, -1.f));       // -1 stop: neutral grey ramp
  CHECK(g[0] == g[1] && g[1] == g[2] && g[0] > 0.f && g[0] < 1.f);
  // Middle grey parameter moves the green band.
  p.middleGrey = 0.09f;
  CHECK(is(colorAt(0.09f), 0.10f, 0.75f, 0.20f));
}

TEST(false_color_in_linear_and_srgb_spaces_reads_the_same_light) {
  for (int space : {1, 2}) {
    Params p = only();
    p.falseColor = true;
    p.space = space;
    Image im = solid(8, 8, vv_from_linear1(0.18f, space));
    process(im, p);
    const float* c = im.at(2, 2);
    // Green, expressed in this working space.
    CHECK_NEAR(c[1], vv_from_linear1(vv_to_linear1(0.75f, VV_SPACE_REC709_G24), space), 1e-4f);
  }
}

TEST(zebras_cover_about_half_of_the_area_above_the_level_only) {
  Params p = only();
  p.zebra = true;
  p.zebraLevel = 0.9f;
  Image im = solid(200, 100, 0.5f);
  for (int y = 0; y < 100; ++y)
    for (int x = 100; x < 200; ++x) im.at(x, y)[0] = im.at(x, y)[1] = im.at(x, y)[2] = 0.95f;
  process(im, p);
  int striped = 0, left = 0;
  for (int y = 0; y < 100; ++y)
    for (int x = 0; x < 200; ++x) {
      const bool white = sameRGB(im.at(x, y), 1.f, 1.f, 1.f);
      if (x >= 100) striped += white;
      else left += white;
    }
  CHECK(left == 0);
  CHECK(striped > 4000 && striped < 6000);  // about half of the 10,000 pixels
  // Diagonal: pixel (x, y) and (x+1, y-1) are on the same stripe.
  bool diag = true;
  for (int y = 1; y < 100; ++y)
    for (int x = 100; x < 199; ++x) diag = diag && (sameRGB(im.at(x, y), 1, 1, 1) == sameRGB(im.at(x + 1, y - 1), 1, 1, 1));
  CHECK(diag);
}

TEST(second_zebra_marks_only_its_range) {
  Params p = only();
  p.zebra2 = true;
  Image im = solid(64, 64, 0.75f), out = im;
  process(out, p);
  bool any = false;
  for (int i = 0; i < 64 * 64; ++i) any = any || sameRGB(&out.px[i * 4], 0.55f, 0.55f, 0.55f);
  CHECK(any);
  Image lo = solid(64, 64, 0.5f), ref = lo;
  process(lo, p);
  CHECK(lo.px == ref.px);
}

TEST(clipping_marks_over_and_under) {
  Params p = only();
  p.clipping = true;
  Image im = solid(4, 1, 0.5f);
  im.at(0, 0)[0] = 1.2f;
  im.at(1, 0)[2] = -0.1f;
  process(im, p);
  CHECK(sameRGB(im.at(0, 0), 1.f, 0.f, 0.f));
  CHECK(sameRGB(im.at(1, 0), 0.f, 0.2f, 1.f));
  CHECK(sameRGB(im.at(2, 0), 0.5f, 0.5f, 0.5f));
}

TEST(split_shows_overlay_on_one_side_with_divider) {
  Params p = only();
  p.falseColor = true;
  p.split = true;
  p.splitPos = 0.5f;
  Image im = solid(200, 100, enc(0.18f)), ref = im;
  process(im, p);
  CHECK(std::memcmp(im.at(20, 50), ref.at(20, 50), 16) == 0);           // left: real image
  CHECK(sameRGB(im.at(180, 50), 0.10f, 0.75f, 0.20f));                   // right: false colour
  CHECK(sameRGB(im.at(100, 50), 1.f, 1.f, 1.f) || sameRGB(im.at(99, 50), 1.f, 1.f, 1.f));  // divider
  p.splitOverlayFirst = true;
  p.splitHorizontal = true;
  Image h = ref;
  process(h, p);
  CHECK(sameRGB(h.at(20, 10), 0.10f, 0.75f, 0.20f));                     // top: overlay
  CHECK(std::memcmp(h.at(20, 90), ref.at(20, 90), 16) == 0);            // bottom: real image
}

TEST(legend_bar_shows_the_scale_left_to_right) {
  Params p = only();
  p.legend = true;
  Image im = solid(400, 200, 0.0f);
  process(im, p);
  const int y = 200 - 2 * 5 + 2;  // inside the bar (height max(4, 2.5 % of 200) = 5)
  CHECK(sameRGB(im.at(82, y), 0.45f, 0.05f, 0.55f));   // left end: crushed
  CHECK(sameRGB(im.at(317, y), 0.90f, 0.05f, 0.05f));  // right end: above white
  CHECK(sameRGB(im.at(10, 10), 0.f, 0.f, 0.f));        // nothing outside the bar
}

TEST(opacity_alpha_transparency_and_finiteness) {
  Params p;  // defaults: false colour + legend
  p.opacity = 0.5f;
  for (auto [w, h] : std::vector<std::pair<int, int>>{{1, 1}, {3, 5}, {257, 129}}) {
    Image im = solid(w, h, 0.3f, 0.6f), ref = im;
    im.at(0, 0)[3] = 0.f;
    ref.at(0, 0)[3] = 0.f;
    process(im, p);
    CHECK(std::memcmp(im.at(0, 0), ref.at(0, 0), 16) == 0);
    for (int i = 0; i < w * h; ++i) {
      CHECK(im.px[i * 4 + 3] == ref.px[i * 4 + 3]);
      for (int c = 0; c < 3; ++c) CHECK(std::isfinite(im.px[i * 4 + c]));
    }
  }
}

// GPU pass emulation: the per-pixel GPU function in both host layouts equals the CPU reference.
TEST(gpu_pass_matches_cpu_in_premiere_and_openfx_layouts) {
  for (const Preset& pr : builtinPresets()) {
    Params p = pr.params;
    p.zebra = p.zebra2 = p.clipping = true;  // exercise everything
    for (const char* scene : vv::scenes::kAll) {
      const Image src = vv::scenes::makeScene(scene, 160, 90);
      Image ref = src;
      process(ref, p);
      const EfInfo info = ef::info(sanitized(p), src.width, src.height);
      for (int layout = 0; layout < 2; ++layout) {  // 0 BGRA top-down, 1 RGBA bottom-up
        const size_t W = 160, H = 90;
        std::vector<gpu::F4> f(W * H);
        for (size_t y = 0; y < H; ++y)
          for (size_t x = 0; x < W; ++x) {
            const float* s = src.at(int(x), int(y));
            f[(layout ? H - 1 - y : y) * W + x] = layout ? gpu::F4{s[0], s[1], s[2], s[3]} : gpu::F4{s[2], s[1], s[0], s[3]};
          }
        const gpu::FrameLayout fl{uint32_t(W), layout, layout};
        for (uint32_t y = 0; y < H; ++y)
          for (uint32_t x = 0; x < W; ++x) gpu::pxEf(f.data(), f.data(), info, fl, x, y);
        bool same = true;
        for (size_t y = 0; y < H; ++y)
          for (size_t x = 0; x < W; ++x) {
            const gpu::F4 q = f[(layout ? H - 1 - y : y) * W + x];
            const float got[4] = {layout ? q.x : q.z, q.y, layout ? q.z : q.x, q.w};
            same = same && std::memcmp(got, ref.at(int(x), int(y)), 16) == 0;
          }
        CHECK(same);
      }
    }
  }
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
  CHECK(std::string(fx.info().ofxParamPrefix) == "ef");
  FakeHost h;
  const Params d = readParams(h), core;
  CHECK(d.falseColor == core.falseColor && std::fabs(d.zebraLevel - core.zebraLevel) < 1e-6f && fx.active(h));
  const int menu = paramIndex(ParamKey::Preset);
  h.setPopup(menu, 3);  // Zebras 95%
  CHECK(fx.paramChanged(menu, h, h));
  CHECK(h.get(ParamKey::ZbEnable) == 1 && h.get(ParamKey::FcEnable) == 0 && h.get(ParamKey::Preset) == 1);
  Image a = vv::scenes::makeScene("night-street", 96, 54), b = a;
  CHECK(fx.render(a, h, 0) && process(b, readParams(h)) && a.px == b.px);
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
