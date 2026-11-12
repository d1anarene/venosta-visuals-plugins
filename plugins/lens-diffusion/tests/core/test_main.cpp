// Lens & Diffusion core tests (no host, no GPU). Build with CMake and run ld_core_tests.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "LensParamsTable.h"
#include "ld_kernels.h"
#include "lens.h"
#include "vv/blur.h"
#include "vv/effect.h"
#include "vv/scenes.h"

using namespace ld;

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
static bool same(const Image& a, const Image& b) { return a.px == b.px; }
static bool finite(const Image& im) {
  return std::all_of(im.px.begin(), im.px.end(), [](float v) { return std::isfinite(v); });
}
static double sumLinear(const Image& im, int c, int space = 2) {
  double s = 0;
  for (int i = 0; i < im.width * im.height; ++i) s += vv_to_linear1(im.px[i * 4 + c], space);
  return s;
}
static LensParams linearOnly() {
  LensParams p;
  p.space = WorkingSpace::Linear;
  return p;
}

TEST(bypass_is_bit_exact) {
  Image im = solid(40, 30, 0.3f, 0.5f, 0.7f), ref = im;
  LensParams p;
  CHECK(!p.anyEffectActive());
  CHECK(process(im, p) && same(im, ref));
  p.diffusion.enabled = true;
  p.mix = 0.f;
  CHECK(process(im, p) && same(im, ref));
  p.mix = 1.f;
  p.diffusion.amount = 0.f;
  CHECK(process(im, p) && same(im, ref));
}

TEST(diffusion_conserves_energy_and_spreads_light) {
  LensParams p = linearOnly();
  p.diffusion = {true, 0.5f, 3.f, 0.5f, 0.f, 0.f};
  Image im = solid(320, 180, 0.f, 0.f, 0.f);
  rect(im, 150, 80, 170, 100, 4.f, 4.f, 4.f);
  const double before = sumLinear(im, 1);
  Image ref = im;
  process(im, p);
  CHECK(std::fabs(sumLinear(im, 1) - before) < 0.01 * before);  // redistributed, not added
  CHECK(im.at(160, 90)[1] < ref.at(160, 90)[1]);                 // the source loses light
  CHECK(im.at(160, 60)[1] > 0.f);                                // ...which lands around it
  CHECK(im.at(160, 60)[1] > im.at(160, 20)[1]);                  // and falls off with distance
}

TEST(mist_lifts_shadows_black_mist_keeps_them) {
  Image base = solid(320, 180, 0.02f, 0.02f, 0.02f);
  rect(base, 100, 60, 220, 120, 0.8f, 0.8f, 0.8f);  // mid-bright area, not a highlight
  LensParams mist = linearOnly(), black = linearOnly();
  mist.diffusion = {true, 0.4f, 4.f, 0.5f, 0.f, 0.f};
  black.diffusion = {true, 0.4f, 4.f, 0.5f, 1.f, 0.f};
  Image a = base, b = base;
  process(a, mist);
  process(b, black);
  // Next to the mid-bright area, mist adds clearly more light into the shadows than black mist
  // (compare the light added on top of the 0.02 shadow).
  CHECK(a.at(90, 90)[0] - 0.02f > 2.0f * (b.at(90, 90)[0] - 0.02f));
  CHECK(b.at(90, 90)[0] - 0.02f > 0.f);
  // Far away, black mist leaves the shadow (almost) untouched.
  CHECK(std::fabs(b.at(5, 5)[0] - 0.02f) < 0.002f);
}

TEST(diffusion_is_neutral_without_warmth_and_tints_with_it) {
  Image im = solid(200, 120, 0.f, 0.f, 0.f);
  rect(im, 95, 55, 105, 65, 5.f, 5.f, 5.f);
  LensParams p = linearOnly();
  p.diffusion = {true, 0.5f, 3.f, 0.5f, 0.5f, 0.f};
  Image n = im;
  process(n, p);
  CHECK_NEAR(n.at(100, 40)[0], n.at(100, 40)[2], 1e-5f);
  p.diffusion.warmth = 1.f;
  Image w = im;
  process(w, p);
  CHECK(w.at(100, 40)[0] > w.at(100, 40)[2]);
}

TEST(aberration_zero_at_centre_grows_to_edges_and_keeps_green) {
  // A white grid: lateral CA separates red/blue at edges far from the centre.
  Image im = solid(400, 300, 0.1f, 0.1f, 0.1f);
  for (int x = 0; x < 400; x += 20) rect(im, x, 0, x + 2, 300, 1.f, 1.f, 1.f);
  LensParams p = linearOnly();
  p.aberration = {true, 6.f, 0.f};
  Image o = im;
  process(o, p);
  bool greenSame = true;
  for (size_t i = 0; i < o.px.size(); i += 4) greenSame = greenSame && std::fabs(o.px[i + 1] - im.px[i + 1]) < 1e-6f;
  CHECK(greenSame);
  auto rbDiff = [&](int x0, int x1) {
    double d = 0;
    for (int x = x0; x < x1; ++x) d += std::fabs(o.at(x, 150)[0] - o.at(x, 150)[2]);
    return d;
  };
  CHECK(rbDiff(195, 205) < 0.05);              // centre: no fringes
  CHECK(rbDiff(370, 400) > 10.0 * rbDiff(195, 205) + 0.2);  // edge: clear fringes
}

TEST(streaks_are_horizontal_and_only_from_highlights) {
  Image im = solid(400, 200, 0.f, 0.f, 0.f);
  rect(im, 198, 98, 202, 102, 8.f, 8.f, 8.f);   // bright point
  rect(im, 40, 150, 120, 190, 0.6f, 0.6f, 0.6f);  // mid-grey area (below threshold)
  LensParams p = linearOnly();
  p.streaks = {true, 0.5f, 1.f, 0.3f, 40.f, StreakColor::Neutral};
  Image o = im;
  process(o, p);
  CHECK(o.at(300, 100)[1] > 1e-3f);    // streak to the right
  CHECK(o.at(100, 100)[1] > 1e-3f);    // and to the left
  CHECK(o.at(200, 120)[1] < 1e-6f);    // nothing above/below: purely horizontal
  CHECK(std::fabs(o.at(80, 170)[1] - 0.6f) < 1e-6f);  // inside the mid-grey area: no streak
  p.streaks.color = StreakColor::Blue;
  Image b = im;
  process(b, p);
  CHECK(b.at(300, 100)[2] > b.at(300, 100)[0]);  // blue streak
}

TEST(horizontal_blur_has_no_vertical_spread_and_conserves) {
  vv::Plane pl(300, 50, 1);
  pl.at(150, 25)[0] = 1.f;
  vv::Plane b = vv::gaussianBlurHorizontal(pl, 40.f);
  double sum = 0;
  for (float v : b.v) sum += v;
  CHECK(std::fabs(sum - 1.0) < 0.02);
  CHECK(b.at(150, 24)[0] == 0.f && b.at(150, 26)[0] == 0.f);
  CHECK(b.at(190, 25)[0] > 0.f && b.at(190, 25)[0] < b.at(150, 25)[0]);
}

TEST(vignette_symmetric_centred_and_signed) {
  LensParams p = linearOnly();
  p.vignette.enabled = true;
  p.vignette.amount = 0.6f;
  for (int style = 0; style < 2; ++style) {
    p.vignette.style = static_cast<VignetteStyle>(style);
    Image im = solid(321, 181, 0.5f, 0.5f, 0.5f);
    process(im, p);
    CHECK_NEAR(im.at(160, 90)[0], 0.5f, 0.01f);                // centre ~untouched
    CHECK(im.at(0, 0)[0] < 0.4f);                             // corners darker
    CHECK_NEAR(im.at(0, 0)[0], im.at(320, 180)[0], 1e-5f);    // symmetric
    CHECK_NEAR(im.at(0, 90)[0], im.at(320, 90)[0], 1e-5f);
  }
  p.vignette.amount = -0.6f;
  Image im = solid(321, 181, 0.3f, 0.3f, 0.3f);
  process(im, p);
  CHECK(im.at(0, 0)[0] > 0.3f);  // negative amount lightens
}

TEST(alpha_transparency_mix_and_odd_sizes) {
  const Preset* pr = findPreset("Vintage Lens");
  CHECK(pr != nullptr);
  for (auto [w, h] : std::vector<std::pair<int, int>>{{1, 1}, {3, 7}, {17, 5}, {257, 131}}) {
    Image im = solid(w, h, 0.6f, 0.4f, 0.3f, 0.7f);
    im.at(0, 0)[3] = 0.f;
    Image ref = im;
    CHECK(process(im, pr->params));
    CHECK(finite(im));
    CHECK(std::memcmp(im.at(0, 0), ref.at(0, 0), 16) == 0);  // transparent pixel untouched
    for (int i = 0; i < w * h; ++i) CHECK(im.px[i * 4 + 3] == ref.px[i * 4 + 3]);
  }
  LensParams p = linearOnly();
  p.vignette.enabled = true;
  p.vignette.amount = 1.f;
  p.vignette.style = VignetteStyle::Smooth;
  p.vignette.size = 0.2f;
  p.vignette.feather = 0.05f;
  Image full = solid(100, 100, 0.5f, 0.5f, 0.5f), half = full;
  process(full, p);
  p.mix = 0.5f;
  process(half, p);
  CHECK_NEAR(half.at(0, 0)[0], 0.5f * (0.5f + full.at(0, 0)[0]), 1e-5f);
}

TEST(every_preset_is_finite_on_every_scene) {
  for (const char* scene : vv::scenes::kAll) {
    for (const Preset& pr : builtinPresets()) {
      Image im = vv::scenes::makeScene(scene, 192, 108);
      CHECK(process(im, pr.params));
      CHECK(finite(im));
    }
  }
}

struct FakeHost : ParamReader, vv::ParamWriter {
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

TEST(param_table_and_effect_interface) {
  const auto& all = allParams();
  CHECK(all.size() == static_cast<size_t>(ParamKey::Count));
  std::vector<int> ids;
  int depth = 0;
  for (int k = 0; k < static_cast<int>(ParamKey::Count); ++k) {
    const ParamSpec& sp = all[static_cast<size_t>(k)];
    CHECK(sp.key == k);
    ids.push_back(sp.diskId);
    if (sp.kind == ParamKind::Float) CHECK(sp.validMin <= sp.sliderMin && sp.sliderMax <= sp.validMax && sp.dflt >= sp.validMin && sp.dflt <= sp.validMax);
    if (sp.kind == ParamKind::Popup) CHECK(sp.items && sp.itemCount == static_cast<int>(vv::splitItems(sp.items).size()));
    if (sp.kind == ParamKind::GroupStart) ++depth;
    if (sp.kind == ParamKind::GroupEnd) --depth;
    CHECK(depth == 0 || depth == 1);
  }
  CHECK(depth == 0);
  std::sort(ids.begin(), ids.end());
  CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
  const vv::Effect& fx = vv::effect();
  CHECK(std::string(fx.info().matchName) == "VVIS LensDiffusion");
  CHECK(std::string(fx.info().ofxParamPrefix) == "ld");
  FakeHost h;
  const LensParams d = readParams(h), core;
  CHECK(d.diffusion.amount == core.diffusion.amount && d.vignette.size == core.vignette.size && !fx.active(h));
  const int menu = paramIndex(ParamKey::Preset);
  h.setPopup(menu, 5);  // Vintage Lens
  CHECK(fx.paramChanged(menu, h, h));
  CHECK(h.get(ParamKey::Preset) == 1 && h.get(ParamKey::CaEnable) == 1 && h.get(ParamKey::VigEnable) == 1);
  CHECK(fx.active(h));
  Image a = solid(64, 36, 0.4f, 0.5f, 0.6f), b = a;
  CHECK(fx.render(a, h, 0) && process(b, readParams(h)) && same(a, b));
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
