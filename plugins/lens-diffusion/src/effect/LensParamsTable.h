// Lens & Diffusion parameter table for the shared host adapters (Premiere Pro, OpenFX).
// Each control has a stable ParamKey and on-disk ID (saved in projects: append only, never
// renumber). Host index = table position + 1 (index 0 is the input layer).
#pragma once
#include <vector>

#include "lens_params.h"
#include "vv/params.h"

namespace ld {

using vv::ParamKind;
using vv::ParamReader;
using vv::ParamSpec;

enum class ParamKey : int {
  Preset, MasterEnable, Mix, WorkingSpace,
  DiffGroup, DiffEnable, DiffAmount, DiffRadius, DiffHalo, DiffBias, DiffWarmth, DiffGroupEnd,
  VigGroup, VigEnable, VigAmount, VigSize, VigRoundness, VigFeather, VigStyle, VigGroupEnd,
  CaGroup, CaEnable, CaAmount, CaEdgeBias, CaGroupEnd,
  StrGroup, StrEnable, StrIntensity, StrThreshold, StrKnee, StrLength, StrColor, StrGroupEnd,
  CaSmear,  // appended (v1.0.17): disk IDs and host indices of existing controls stay the same
  Count
};

// Append only: the chosen index is saved in projects.
#define LD_PRESET_ITEMS \
  "(choose to apply)|Subtle Mist|Strong Mist|Highlight Mist|Vintage Lens|Anamorphic Night|Natural Vignette|Off|" \
  "Extreme Fringe"
#define LD_PRESET_COUNT 9

inline const std::vector<ParamSpec>& allParams() {
  static const std::vector<ParamSpec> specs = [] {
    const LensParams d;
    std::vector<ParamSpec> v;
    using K = ParamKey;
    using T = ParamKind;
    auto add = [&](K key, T kind, const char* name, int id, double vmin = 0, double vmax = 0, double smin = 0,
                   double smax = 0, double dflt = 0, int precision = 2) {
      ParamSpec s{static_cast<int>(key), kind, name, id};
      s.validMin = vmin; s.validMax = vmax; s.sliderMin = smin; s.sliderMax = smax; s.dflt = dflt;
      s.precision = precision;
      v.push_back(s);
      return &v.back();
    };
    auto popup = [&](K key, const char* name, int id, const char* items, int count, int dflt) {
      ParamSpec* s = add(key, T::Popup, name, id, 0, 0, 0, 0, dflt);
      s->items = items; s->itemCount = count;
      return s;
    };
    auto group = [&](K key, const char* name, int id) { add(key, T::GroupStart, name, id)->startCollapsed = true; };
    ParamSpec* s = popup(K::Preset, "Preset", 1, LD_PRESET_ITEMS, LD_PRESET_COUNT, 1);
    s->supervise = true; s->cannotTimeVary = true;
    add(K::MasterEnable, T::Checkbox, "Master Enable", 2, 0, 0, 0, 0, d.masterEnable);
    add(K::Mix, T::Float, "Global Mix", 3, 0, 100, 0, 100, d.mix * 100.0, 1)->percent = true;
    popup(K::WorkingSpace, "Working Space", 4, "Rec.709 / Gamma 2.4 (SDR)|sRGB|Linear", 3, 1)->cannotTimeVary = true;

    group(K::DiffGroup, "Diffusion", 5);
    add(K::DiffEnable, T::Checkbox, "Diffusion Enable", 6, 0, 0, 0, 0, d.diffusion.enabled);
    add(K::DiffAmount, T::Float, "Amount", 7, 0, 1, 0, 1, d.diffusion.amount);
    add(K::DiffRadius, T::Float, "Size (% of height)", 8, 0.2, 25, 0.5, 10, d.diffusion.radius, 1);
    add(K::DiffHalo, T::Float, "Halo", 9, 0, 1, 0, 1, d.diffusion.halo);
    add(K::DiffBias, T::Float, "Highlight Bias (mist to black mist)", 10, 0, 1, 0, 1, d.diffusion.highlightBias);
    add(K::DiffWarmth, T::Float, "Warmth", 11, -1, 1, -1, 1, d.diffusion.warmth);
    add(K::DiffGroupEnd, T::GroupEnd, "", 12);

    group(K::VigGroup, "Vignette", 13);
    add(K::VigEnable, T::Checkbox, "Vignette Enable", 14, 0, 0, 0, 0, d.vignette.enabled);
    add(K::VigAmount, T::Float, "Amount (darken / lighten)", 15, -1, 1, -1, 1, d.vignette.amount);
    add(K::VigSize, T::Float, "Size", 16, 0.1, 1.5, 0.1, 1.5, d.vignette.size);
    add(K::VigRoundness, T::Float, "Roundness", 17, 0, 1, 0, 1, d.vignette.roundness);
    add(K::VigFeather, T::Float, "Feather (Smooth style)", 18, 0.05, 1, 0.05, 1, d.vignette.feather);
    popup(K::VigStyle, "Style", 19, "Natural (optical)|Smooth", 2, 1);
    add(K::VigGroupEnd, T::GroupEnd, "", 20);

    group(K::CaGroup, "Chromatic Aberration", 21);
    add(K::CaEnable, T::Checkbox, "Aberration Enable", 22, 0, 0, 0, 0, d.aberration.enabled);
    // Valid range widened from +-10 to +-300 for exaggerated looks (slider +-50, larger values can
    // be typed); same disk ID, saved values unchanged.
    add(K::CaAmount, T::Float, "Amount (px at corner @1080)", 23, -300, 300, -50, 50, d.aberration.amount);
    add(K::CaEdgeBias, T::Float, "Edge Bias", 24, 0, 1, 0, 1, d.aberration.edgeBias);
    add(K::CaGroupEnd, T::GroupEnd, "", 25);

    group(K::StrGroup, "Anamorphic Streaks", 26);
    add(K::StrEnable, T::Checkbox, "Streaks Enable", 27, 0, 0, 0, 0, d.streaks.enabled);
    add(K::StrIntensity, T::Float, "Intensity", 28, 0, 1, 0, 1, d.streaks.intensity);
    add(K::StrThreshold, T::Float, "Threshold", 29, 0, 16, 0, 4, d.streaks.threshold);
    add(K::StrKnee, T::Float, "Soft Knee", 30, 0.001, 4, 0.01, 1, d.streaks.knee);
    add(K::StrLength, T::Float, "Length (% of width)", 31, 1, 100, 5, 60, d.streaks.length, 1);
    popup(K::StrColor, "Color", 32, "Blue|Cyan|Neutral|Amber", 4, 1);
    add(K::StrGroupEnd, T::GroupEnd, "", 33);
    // Appended after the last group so every existing control keeps its host index.
    add(K::CaSmear, T::Float, "Aberration Spectral Smear", 34, 0, 1, 0, 1, d.aberration.smear);
    return v;
  }();
  return specs;
}

inline int paramIndex(ParamKey k) { return vv::hostIndex(allParams(), static_cast<int>(k)); }
inline const ParamSpec& specFor(ParamKey k) { return allParams()[static_cast<size_t>(paramIndex(k) - 1)]; }

inline LensParams readParams(const ParamReader& r) {
  using K = ParamKey;
  auto num = [&](K k) { return static_cast<float>(r.number(paramIndex(k))); };
  auto flag = [&](K k) { return r.flag(paramIndex(k)); };
  auto pop = [&](K k) { return r.popup(paramIndex(k)); };
  LensParams p;
  p.masterEnable = flag(K::MasterEnable);
  p.mix = num(K::Mix) / 100.f;
  p.space = static_cast<WorkingSpace>(pop(K::WorkingSpace) - 1);
  p.diffusion = {flag(K::DiffEnable), num(K::DiffAmount), num(K::DiffRadius), num(K::DiffHalo), num(K::DiffBias),
                 num(K::DiffWarmth)};
  p.vignette = {flag(K::VigEnable), num(K::VigAmount), num(K::VigSize), num(K::VigRoundness), num(K::VigFeather),
                static_cast<VignetteStyle>(pop(K::VigStyle) - 1)};
  p.aberration = {flag(K::CaEnable), num(K::CaAmount), num(K::CaEdgeBias), num(K::CaSmear)};
  p.streaks = {flag(K::StrEnable), num(K::StrIntensity), num(K::StrThreshold), num(K::StrKnee), num(K::StrLength),
               static_cast<StreakColor>(pop(K::StrColor) - 1)};
  return sanitized(p);
}

inline const char* presetMenuName(int value1Based) {
  static const char* kNames[] = {nullptr, "Subtle Mist", "Strong Mist", "Highlight Mist", "Vintage Lens",
                                 "Anamorphic Night", "Natural Vignette", "Off", "Extreme Fringe"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == LD_PRESET_COUNT, "preset menu and names out of sync");
  return value1Based >= 1 && value1Based <= LD_PRESET_COUNT ? kNames[value1Based - 1] : nullptr;
}

/** Every control a preset sets: numbers, checkboxes 0/1, popups 1-based. */
template <typename F>
inline void forEachPresetValue(const LensParams& p, F&& set) {
  using K = ParamKey;
  set(K::DiffEnable, p.diffusion.enabled ? 1.0 : 0.0);
  set(K::DiffAmount, p.diffusion.amount);
  set(K::DiffRadius, p.diffusion.radius);
  set(K::DiffHalo, p.diffusion.halo);
  set(K::DiffBias, p.diffusion.highlightBias);
  set(K::DiffWarmth, p.diffusion.warmth);
  set(K::VigEnable, p.vignette.enabled ? 1.0 : 0.0);
  set(K::VigAmount, p.vignette.amount);
  set(K::VigSize, p.vignette.size);
  set(K::VigRoundness, p.vignette.roundness);
  set(K::VigFeather, p.vignette.feather);
  set(K::VigStyle, static_cast<double>(static_cast<int>(p.vignette.style) + 1));
  set(K::CaEnable, p.aberration.enabled ? 1.0 : 0.0);
  set(K::CaAmount, p.aberration.amount);
  set(K::CaEdgeBias, p.aberration.edgeBias);
  set(K::CaSmear, p.aberration.smear);
  set(K::StrEnable, p.streaks.enabled ? 1.0 : 0.0);
  set(K::StrIntensity, p.streaks.intensity);
  set(K::StrThreshold, p.streaks.threshold);
  set(K::StrKnee, p.streaks.knee);
  set(K::StrLength, p.streaks.length);
  set(K::StrColor, static_cast<double>(static_cast<int>(p.streaks.color) + 1));
}

}  // namespace ld
