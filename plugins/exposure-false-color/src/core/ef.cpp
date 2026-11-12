#include "ef.h"

#include <algorithm>
#include <cmath>

#include "vv/parallel.h"

namespace ef {
namespace {
float clampf(float v, float lo, float hi, float fallback) {
  return std::isfinite(v) ? std::min(std::max(v, lo), hi) : fallback;
}
}  // namespace

Params sanitized(const Params& in) {
  const Params d;
  Params p = in;
  p.opacity = clampf(p.opacity, 0.f, 1.f, d.opacity);
  if (p.space < 0 || p.space > 2) p.space = d.space;
  p.middleGrey = clampf(p.middleGrey, 0.01f, 1.f, d.middleGrey);
  p.zebraLevel = clampf(p.zebraLevel, 0.f, 1.1f, d.zebraLevel);
  p.zebra2Low = clampf(p.zebra2Low, 0.f, 1.1f, d.zebra2Low);
  p.zebra2High = clampf(p.zebra2High, p.zebra2Low, 1.1f, d.zebra2High);
  p.stripe = clampf(p.stripe, 2.f, 64.f, d.stripe);
  p.splitPos = clampf(p.splitPos, 0.f, 1.f, d.splitPos);
  return p;
}

EfInfo info(const Params& p, int w, int h) {
  EfInfo i{};
  i.width = static_cast<unsigned int>(w);
  i.height = static_cast<unsigned int>(h);
  i.space = p.space;
  i.falseColor = p.falseColor;
  i.legend = p.legend;
  i.zebra = p.zebra;
  i.zebra2 = p.zebra2;
  i.clipping = p.clipping;
  i.split = p.split;
  i.splitHorizontal = p.splitHorizontal;
  i.splitOverlayFirst = p.splitOverlayFirst;
  i.middleGrey = p.middleGrey;
  i.zebraLevel = p.zebraLevel;
  i.zebra2Low = p.zebra2Low;
  i.zebra2High = p.zebra2High;
  i.stripePx = p.stripe * static_cast<float>(h) / 1080.0f;
  i.splitPos = p.splitPos;
  i.opacity = p.opacity;
  return i;
}

bool process(Image& img, const Params& raw) {
  if (img.empty()) return false;
  const Params p = sanitized(raw);
  if (!p.anyActive()) return true;  // bypass: untouched
  const EfInfo inf = info(p, img.width, img.height);
  vv::parallelRows(img.height, [&](int y) {
    for (int x = 0; x < img.width; ++x) {
      float* px = img.at(x, y);
      if (px[3] <= 0.f) continue;  // never invent colour where the layer is fully transparent
      const vv_float3 o = ef_shade(vv_make3(px[0], px[1], px[2]), static_cast<float>(x), static_cast<float>(y), inf);
      const float out[3] = {o.x, o.y, o.z};
      for (int c = 0; c < 3; ++c) {
        const float v = p.opacity >= 1.f ? out[c] : px[c] + (out[c] - px[c]) * p.opacity;
        if (std::isfinite(v)) px[c] = v;
      }
    }
  });
  return true;
}

const std::vector<Preset>& builtinPresets() {
  static const std::vector<Preset> v = [] {
    std::vector<Preset> r;
    auto add = [&](const char* n, auto&& fn) {
      Params p;
      fn(p);
      r.push_back({n, p});
    };
    add("False Color", [](Params&) {});
    add("Zebras 95%", [](Params& p) { p.falseColor = false; p.legend = false; p.zebra = true; });
    add("Skin Zebras 70-80%", [](Params& p) { p.falseColor = false; p.legend = false; p.zebra2 = true; });
    add("Clipping Check", [](Params& p) { p.falseColor = false; p.legend = false; p.clipping = true; });
    add("Split False Color", [](Params& p) { p.split = true; });
    return r;
  }();
  return v;
}

const Preset* findPreset(const std::string& name) {
  for (const auto& p : builtinPresets())
    if (p.name == name) return &p;
  return nullptr;
}

}  // namespace ef
