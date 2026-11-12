// Retro parameter table for the shared host adapters. Disk IDs are saved in projects: append only,
// never renumber. Host index = table position + 1.
#pragma once
#include <vector>

#include "rt.h"
#include "vv/params.h"

namespace rt {

using vv::ParamKind;
using vv::ParamReader;
using vv::ParamSpec;

enum class ParamKey : int {
  Preset, MasterEnable, Mix, Seed, Animate,
  BleedGroup, BleedEnable, LumaSoft, Ringing, ChromaBlur, ChromaShift, Saturation, BleedGroupEnd,
  TapeGroup, TapeEnable, Jitter, Tracking, Dropouts, HeadSwitch, TapeGroupEnd,
  NoiseGroup, NoiseEnable, LumaNoise, ChromaNoise, NoiseGroupEnd,
  CrtGroup, CrtEnable, Scanlines, LineCount, MaskType, MaskStrength, Curvature, CrtGroupEnd,
  Count
};

#define RT_PRESET_ITEMS \
  "(choose to apply)|Home Video Tape|Worn Tape|Broadcast 80s|CRT Monitor|Subtle Analog|Off"
#define RT_PRESET_COUNT 7

inline const std::vector<ParamSpec>& allParams() {
  static const std::vector<ParamSpec> specs = [] {
    const Params d;
    std::vector<ParamSpec> v;
    using K = ParamKey;
    using T = ParamKind;
    auto add = [&](K key, T kind, const char* name, int id, double vmin = 0, double vmax = 0, double smin = 0,
                   double smax = 0, double dflt = 0, int precision = 2) -> ParamSpec& {
      ParamSpec s{static_cast<int>(key), kind, name, id};
      s.validMin = vmin; s.validMax = vmax; s.sliderMin = smin; s.sliderMax = smax; s.dflt = dflt; s.precision = precision;
      v.push_back(s);
      return v.back();
    };
    auto pct = [&](K key, const char* name, int id, double vmax, double value) {
      add(key, T::Float, name, id, 0, vmax, 0, vmax, value * 100.0, 1).percent = true;
    };
    auto popup = [&](K key, const char* name, int id, const char* items, int count, int dflt) -> ParamSpec& {
      ParamSpec& s = add(key, T::Popup, name, id, 0, 0, 0, 0, dflt);
      s.items = items; s.itemCount = count;
      return s;
    };
    auto group = [&](K key, const char* name, int id) { add(key, T::GroupStart, name, id).startCollapsed = true; };
    ParamSpec& pre = popup(K::Preset, "Preset", 1, RT_PRESET_ITEMS, RT_PRESET_COUNT, 1);
    pre.supervise = true; pre.cannotTimeVary = true;
    add(K::MasterEnable, T::Checkbox, "Master Enable", 2, 0, 0, 0, 0, d.masterEnable);
    pct(K::Mix, "Mix", 3, 100, d.mix);
    add(K::Seed, T::IntSlider, "Seed", 4, 0, 99999, 0, 999, double(d.seed), 0);
    add(K::Animate, T::Checkbox, "Animate", 5, 0, 0, 0, 0, d.animate);
    group(K::BleedGroup, "Color Bleed", 6);
    add(K::BleedEnable, T::Checkbox, "Color Bleed Enable", 7, 0, 0, 0, 0, d.bleed);
    add(K::LumaSoft, T::Float, "Luma Softness (px @1080)", 8, 0, 6, 0, 4, d.lumaSoft, 2);
    pct(K::Ringing, "Ringing", 9, 100, d.ringing);
    add(K::ChromaBlur, T::Float, "Chroma Blur (px @1080)", 10, 0, 30, 0, 20, d.chromaBlur, 1);
    add(K::ChromaShift, T::Float, "Chroma Shift (px @1080)", 11, -10, 10, -6, 6, d.chromaShift, 1);
    pct(K::Saturation, "Saturation", 12, 200, d.saturation);
    add(K::BleedGroupEnd, T::GroupEnd, "", 13);
    group(K::TapeGroup, "Tape", 14);
    add(K::TapeEnable, T::Checkbox, "Tape Enable", 15, 0, 0, 0, 0, d.tape);
    add(K::Jitter, T::Float, "Line Jitter (px @1080)", 16, 0, 10, 0, 6, d.jitter, 1);
    pct(K::Tracking, "Tracking Error", 17, 100, d.tracking);
    pct(K::Dropouts, "Dropouts", 18, 100, d.dropouts);
    pct(K::HeadSwitch, "Head Switching", 19, 100, d.headSwitch);
    add(K::TapeGroupEnd, T::GroupEnd, "", 20);
    group(K::NoiseGroup, "Noise", 21);
    add(K::NoiseEnable, T::Checkbox, "Noise Enable", 22, 0, 0, 0, 0, d.noise);
    pct(K::LumaNoise, "Luma Noise", 23, 100, d.lumaNoise);
    pct(K::ChromaNoise, "Chroma Noise", 24, 100, d.chromaNoise);
    add(K::NoiseGroupEnd, T::GroupEnd, "", 25);
    group(K::CrtGroup, "CRT", 26);
    add(K::CrtEnable, T::Checkbox, "CRT Enable", 27, 0, 0, 0, 0, d.crt);
    pct(K::Scanlines, "Scanlines", 28, 100, d.scanlines);
    add(K::LineCount, T::Float, "Line Count", 29, 60, 1080, 120, 720, d.lineCount, 0);
    popup(K::MaskType, "Mask", 30, "None|Aperture Grille|Shadow Mask", 3, d.maskType + 1);
    pct(K::MaskStrength, "Mask Strength", 31, 100, d.maskStrength);
    pct(K::Curvature, "Screen Curvature", 32, 100, d.curvature);
    add(K::CrtGroupEnd, T::GroupEnd, "", 33);
    return v;
  }();
  return specs;
}

inline int paramIndex(ParamKey k) { return vv::hostIndex(allParams(), static_cast<int>(k)); }
inline const ParamSpec& specFor(ParamKey k) { return allParams()[static_cast<size_t>(paramIndex(k) - 1)]; }

inline Params readParams(const ParamReader& r) {
  using K = ParamKey;
  auto num = [&](K k) { return static_cast<float>(r.number(paramIndex(k))); };
  auto flag = [&](K k) { return r.flag(paramIndex(k)); };
  Params p;
  p.masterEnable = flag(K::MasterEnable);
  p.mix = num(K::Mix) / 100.f;
  p.seed = static_cast<int>(num(K::Seed));
  p.animate = flag(K::Animate);
  p.bleed = flag(K::BleedEnable);
  p.lumaSoft = num(K::LumaSoft);
  p.ringing = num(K::Ringing) / 100.f;
  p.chromaBlur = num(K::ChromaBlur);
  p.chromaShift = num(K::ChromaShift);
  p.saturation = num(K::Saturation) / 100.f;
  p.tape = flag(K::TapeEnable);
  p.jitter = num(K::Jitter);
  p.tracking = num(K::Tracking) / 100.f;
  p.dropouts = num(K::Dropouts) / 100.f;
  p.headSwitch = num(K::HeadSwitch) / 100.f;
  p.noise = flag(K::NoiseEnable);
  p.lumaNoise = num(K::LumaNoise) / 100.f;
  p.chromaNoise = num(K::ChromaNoise) / 100.f;
  p.crt = flag(K::CrtEnable);
  p.scanlines = num(K::Scanlines) / 100.f;
  p.lineCount = num(K::LineCount);
  p.maskType = r.popup(paramIndex(K::MaskType)) - 1;
  p.maskStrength = num(K::MaskStrength) / 100.f;
  p.curvature = num(K::Curvature) / 100.f;
  return sanitized(p);
}

inline const char* presetMenuName(int value1Based) {
  static const char* kNames[] = {nullptr, "Home Video Tape", "Worn Tape", "Broadcast 80s", "CRT Monitor",
                                 "Subtle Analog", "Off"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == RT_PRESET_COUNT, "preset menu and names out of sync");
  return value1Based >= 1 && value1Based <= RT_PRESET_COUNT ? kNames[value1Based - 1] : nullptr;
}

/** Every look control a preset sets (not Master Enable, Mix, Seed, Animate): panel units,
 *  checkboxes 0/1, popups 1-based. */
template <typename F>
inline void forEachPresetValue(const Params& p, F&& set) {
  using K = ParamKey;
  set(K::BleedEnable, p.bleed ? 1.0 : 0.0);
  set(K::LumaSoft, p.lumaSoft);
  set(K::Ringing, p.ringing * 100.0);
  set(K::ChromaBlur, p.chromaBlur);
  set(K::ChromaShift, p.chromaShift);
  set(K::Saturation, p.saturation * 100.0);
  set(K::TapeEnable, p.tape ? 1.0 : 0.0);
  set(K::Jitter, p.jitter);
  set(K::Tracking, p.tracking * 100.0);
  set(K::Dropouts, p.dropouts * 100.0);
  set(K::HeadSwitch, p.headSwitch * 100.0);
  set(K::NoiseEnable, p.noise ? 1.0 : 0.0);
  set(K::LumaNoise, p.lumaNoise * 100.0);
  set(K::ChromaNoise, p.chromaNoise * 100.0);
  set(K::CrtEnable, p.crt ? 1.0 : 0.0);
  set(K::Scanlines, p.scanlines * 100.0);
  set(K::LineCount, p.lineCount);
  set(K::MaskType, double(p.maskType + 1));
  set(K::MaskStrength, p.maskStrength * 100.0);
  set(K::Curvature, p.curvature * 100.0);
}

}  // namespace rt
