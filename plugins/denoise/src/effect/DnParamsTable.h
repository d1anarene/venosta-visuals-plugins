// Denoise parameter table (research prototype). Disk IDs append-only once released.
#pragma once
#include <vector>

#include "vv/params.h"

namespace dn {

using vv::ParamKind;
using vv::ParamReader;
using vv::ParamSpec;

enum class ParamKey : int {
  MasterEnable, Mix, View, Radius, LumaStrength, ChromaStrength, Detail, NoiseLevel, Count
};

struct EffectParams {
  bool masterEnable = true;
  float mix = 1.f;
  int view = 0;            // 0 result, 1 removed noise (x4 + grey), 2 input
  int radius = 2;          // temporal radius (frames each side) where the host supplies frames
  float lumaStrength = 1.f, chromaStrength = 1.5f;
  float detail = 0.5f;     // 0..1: 1 keeps the most detail (less spatial smoothing after fusion)
  float noiseLevel = 1.f;  // scales the estimated noise profile
};

inline const std::vector<ParamSpec>& allParams() {
  static const std::vector<ParamSpec> specs = [] {
    std::vector<ParamSpec> v;
    using K = ParamKey;
    using T = ParamKind;
    auto add = [&](K key, T kind, const char* name, int id, double vmin, double vmax, double smin, double smax,
                   double dflt, int precision = 2) -> ParamSpec& {
      ParamSpec s{static_cast<int>(key), kind, name, id};
      s.validMin = vmin; s.validMax = vmax; s.sliderMin = smin; s.sliderMax = smax; s.dflt = dflt; s.precision = precision;
      v.push_back(s);
      return v.back();
    };
    add(K::MasterEnable, T::Checkbox, "Master Enable", 1, 0, 0, 0, 0, 1);
    add(K::Mix, T::Float, "Mix", 2, 0, 100, 0, 100, 100, 1).percent = true;
    ParamSpec& view = add(K::View, T::Popup, "View", 3, 0, 0, 0, 0, 1);
    view.items = "Result|Removed Noise|Input";
    view.itemCount = 3;
    add(K::Radius, T::IntSlider, "Temporal Frames (each side)", 4, 0, 3, 0, 3, 2, 0);
    add(K::LumaStrength, T::Float, "Luma Strength", 5, 0, 4, 0, 2, 1.0);
    add(K::ChromaStrength, T::Float, "Chroma Strength", 6, 0, 6, 0, 3, 1.5);
    add(K::Detail, T::Float, "Detail", 7, 0, 100, 0, 100, 50, 0).percent = true;
    add(K::NoiseLevel, T::Float, "Noise Level", 8, 0.25, 4, 0.5, 2, 1.0);
    return v;
  }();
  return specs;
}

inline int paramIndex(ParamKey k) { return vv::hostIndex(allParams(), static_cast<int>(k)); }

inline EffectParams readParams(const ParamReader& r) {
  using K = ParamKey;
  EffectParams p;
  p.masterEnable = r.flag(paramIndex(K::MasterEnable));
  p.mix = float(r.number(paramIndex(K::Mix))) / 100.f;
  p.view = r.popup(paramIndex(K::View)) - 1;
  p.radius = int(r.number(paramIndex(K::Radius)));
  p.lumaStrength = float(r.number(paramIndex(K::LumaStrength)));
  p.chromaStrength = float(r.number(paramIndex(K::ChromaStrength)));
  p.detail = float(r.number(paramIndex(K::Detail))) / 100.f;
  p.noiseLevel = float(r.number(paramIndex(K::NoiseLevel)));
  auto clampf = [](float v, float lo, float hi, float d) { return v == v ? (v < lo ? lo : (v > hi ? hi : v)) : d; };
  p.mix = clampf(p.mix, 0, 1, 1);
  p.view = p.view < 0 || p.view > 2 ? 0 : p.view;
  p.radius = p.radius < 0 ? 0 : (p.radius > 3 ? 3 : p.radius);
  p.lumaStrength = clampf(p.lumaStrength, 0, 4, 1);
  p.chromaStrength = clampf(p.chromaStrength, 0, 6, 1.5f);
  p.detail = clampf(p.detail, 0, 1, 0.5f);
  p.noiseLevel = clampf(p.noiseLevel, 0.25f, 4, 1);
  return p;
}

}  // namespace dn
