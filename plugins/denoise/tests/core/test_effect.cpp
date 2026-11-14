// Denoise prototype effect through the vv::Effect interface (no host): spatial render, temporal
// render with a window (incl. missing neighbours), bypass, view modes, alpha.
#include <cmath>
#include <cstdio>
#include <vector>

#include "DnParamsTable.h"
#include "dn_metrics.h"
#include "dn_synth.h"
#include "dn_testimages.h"
#include "vv/effect.h"

using namespace dn;
using namespace dn::eval;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

struct Host : vv::ParamReader {
  std::vector<double> v;
  Host() { const auto& a = allParams(); v.assign(a.size() + 1, 0); for (size_t i = 0; i < a.size(); ++i) v[i + 1] = a[i].dflt; }
  void set(ParamKey k, double x) { v[size_t(paramIndex(k))] = x; }
  double number(int i) const override { return v[size_t(i)]; }
  bool flag(int i) const override { return v[size_t(i)] != 0; }
  int popup(int i) const override { return int(v[size_t(i)]); }
};

struct Writer : vv::ParamWriter {
  Host& h;
  explicit Writer(Host& host) : h(host) {}
  void setNumber(int i, double x) override { h.v[size_t(i)] = x; }
  void setFlag(int i, bool x) override { h.v[size_t(i)] = x; }
  void setPopup(int i, int x) override { h.v[size_t(i)] = x; }
};

int main() {
  const vv::Effect& fx = vv::effect();
  CHECK(fx.maxTemporalRadius() == 3);
  Host h;
  CHECK(fx.active(h) && fx.temporalRadius(h) == 2);
  const vv::Image clean = makeTestImage("skin", 192, 108);
  std::vector<Synth> s;
  for (int t = 0; t < 5; ++t) s.push_back(synthesize(clean, NoiseSim(), 9u, t));
  for (auto& f : s) for (size_t i = 3; i < f.noisy.px.size(); i += 4) f.noisy.px[i] = 0.6f;
  // Spatial (Premiere CPU path).
  vv::Image a = s[2].noisy;
  CHECK(fx.render(a, h, 2));
  CHECK(psnr(a, s[2].clean) > psnr(s[2].noisy, s[2].clean) + 5);
  // Temporal with a full window, then with missing neighbours (clip edge).
  std::vector<const vv::Image*> win = {&s[0].noisy, &s[1].noisy, &s[2].noisy, &s[3].noisy, &s[4].noisy};
  vv::Image t = s[2].noisy;
  CHECK(fx.renderTemporal(t, win, 2, h, 2));
  CHECK(psnr(t, s[2].clean) > psnr(a, s[2].clean));
  std::vector<const vv::Image*> edge = {nullptr, nullptr, &s[2].noisy, &s[3].noisy, nullptr};
  vv::Image e = s[2].noisy;
  CHECK(fx.renderTemporal(e, edge, 2, h, 2));
  CHECK(psnr(e, s[2].clean) > psnr(s[2].noisy, s[2].clean) + 5);
  bool alpha = true, finite = true;
  for (size_t i = 0; i < t.px.size(); ++i) { if (i % 4 == 3) alpha = alpha && t.px[i] == 0.6f; finite = finite && std::isfinite(t.px[i]); }
  CHECK(alpha && finite);
  // Presets: "Strong" (item 4) writes its values and the menu returns to (Custom).
  {
    Host p;
    Writer w(p);
    p.set(ParamKey::Preset, 4);
    CHECK(fx.paramChanged(paramIndex(ParamKey::Preset), p, w));
    CHECK(p.v[size_t(paramIndex(ParamKey::LumaStrength))] == 1.0 && p.v[size_t(paramIndex(ParamKey::Preset))] == 1);
    CHECK(std::fabs(p.v[size_t(paramIndex(ParamKey::Detail))] - 30.0) < 1e-4);
    CHECK(!fx.paramChanged(paramIndex(ParamKey::Mix), p, w));
    // Quality Preview renders too (smaller estimate crop).
    p.set(ParamKey::Quality, 1);
    vv::Image q = s[2].noisy;
    CHECK(fx.render(q, p, 2));
    CHECK(psnr(q, s[2].clean) > psnr(s[2].noisy, s[2].clean) + 3);
  }
  // Bypass and views.
  h.set(ParamKey::MasterEnable, 0);
  CHECK(!fx.active(h));
  h.set(ParamKey::MasterEnable, 1);
  h.set(ParamKey::View, 3);  // Input
  vv::Image in = s[2].noisy;
  fx.render(in, h, 2);
  CHECK(in.px == s[2].noisy.px);
  h.set(ParamKey::View, 2);  // Removed noise: around 0.5 grey
  vv::Image rn = s[2].noisy;
  fx.render(rn, h, 2);
  double m = 0;
  for (size_t i = 0; i < rn.px.size(); i += 4) m += rn.px[i + 1];
  CHECK(std::fabs(m / double(rn.px.size() / 4) - 0.5) < 0.05);
  std::printf("%s (%d failures)\n", g_fail ? "FAIL" : "effect: ok", g_fail);
  return g_fail ? 1 : 0;
}
