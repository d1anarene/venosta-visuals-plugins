// Film Effects parameter table, used by the shared host adapters (Premiere Pro, OpenFX) through
// vv::Effect. Header-only, no host SDK types, unit-tested in tests/core.
//
// Each control has a stable ParamKey and a stable on-disk ID (saved in projects; append only,
// never renumber). The panel shows every control in table order; the host parameter index is the
// position + 1 (index 0 is the input layer). (Standalone Bloom/Halation/Grain variants with
// subsets of these controls existed until v1.0.11.)
#pragma once
#include <cstdint>
#include <vector>

#include "params/params.h"
#include "vv/params.h"

namespace fe {

using vv::ParamKind;
using vv::ParamReader;
using vv::ParamSpec;
using vv::frameKeyFromTime;

enum class ParamKey : int {
  Preset,
  MasterEnable,
  Mix,
  WorkingSpace,
  Quality,
  BloomGroup,
  BloomEnable,
  BloomIntensity,
  BloomThreshold,
  BloomKnee,
  BloomRadius,
  BloomGroupEnd,
  HalGroup,
  HalEnable,
  HalIntensity,
  HalThreshold,
  HalRadius,
  HalWarmth,
  HalGroupEnd,
  GrainGroup,
  GrainEnable,
  GrainAmount,
  GrainSize,
  GrainRoughness,
  GrainShadows,
  GrainMidtones,
  GrainHighlights,
  GrainColor,
  GrainChroma,
  GrainSeed,
  GrainAnimate,
  GrainGroupEnd,
  GateGroup,
  GateEnable,
  GateWeave,
  GateFlicker,
  GateEdge,
  GateEdgeSoftness,
  GateEdgeRoundness,
  GateDust,
  GateScratches,
  GateLook,
  GateSeed,
  GateGroupEnd,
  Count
};

// Append only: the chosen index is saved in projects.
#define FE_PRESET_ITEMS \
  "(choose to apply)|Subtle Film|Soft Highlight Glow|Night Lights|Fine 35mm Grain|Coarse 16mm Grain|Halation Only|Off|Old Projector"
#define FE_PRESET_COUNT 9

/** Every control, with defaults taken from the core's FilmParams. */
inline const std::vector<ParamSpec>& allParams() {
  static const std::vector<ParamSpec> specs = [] {
    const FilmParams d;
    std::vector<ParamSpec> v;
    // Same fields as vv::ParamSpec, but keyed by the typed ParamKey for readability.
    struct S {
      ParamKey key;
      ParamKind kind;
      const char* name;
      int diskId;
      double validMin = 0, validMax = 0, sliderMin = 0, sliderMax = 0, dflt = 0;
      int precision = 2;
      bool percent = false;
      const char* items = nullptr;
      int itemCount = 0;
      bool supervise = false, cannotTimeVary = false, startCollapsed = false;
    };
    auto add = [&](const S& s) {
      ParamSpec o{static_cast<int>(s.key), s.kind, s.name, s.diskId};
      o.validMin = s.validMin; o.validMax = s.validMax; o.sliderMin = s.sliderMin; o.sliderMax = s.sliderMax;
      o.dflt = s.dflt; o.precision = s.precision; o.percent = s.percent; o.items = s.items; o.itemCount = s.itemCount;
      o.supervise = s.supervise; o.cannotTimeVary = s.cannotTimeVary; o.startCollapsed = s.startCollapsed;
      v.push_back(o);
    };
    using K = ParamKey;
    using T = ParamKind;
    S s{};
    s = {K::Preset, T::Popup, "Preset", 1};
    s.items = FE_PRESET_ITEMS; s.itemCount = FE_PRESET_COUNT; s.dflt = 1; s.supervise = true; s.cannotTimeVary = true; add(s);
    s = {K::MasterEnable, T::Checkbox, "Master Enable", 2}; s.dflt = d.masterEnable; add(s);
    s = {K::Mix, T::Float, "Global Mix", 3, 0, 100, 0, 100, d.mix * 100.0, 1, true}; add(s);
    s = {K::WorkingSpace, T::Popup, "Working Space", 4};
    s.items = "Rec.709 / Gamma 2.4 (SDR)|sRGB|Linear"; s.itemCount = 3; s.dflt = 1; s.cannotTimeVary = true; add(s);
    s = {K::Quality, T::Popup, "Processing Quality", 5};
    s.items = "Normal|High"; s.itemCount = 2; s.dflt = 1; s.cannotTimeVary = true; add(s);

    s = {K::BloomGroup, T::GroupStart, "Bloom", 6}; s.startCollapsed = true; add(s);
    s = {K::BloomEnable, T::Checkbox, "Bloom Enable", 7}; s.dflt = d.bloom.enabled; add(s);
    add({K::BloomIntensity, T::Float, "Intensity", 8, 0, 1, 0, 1, d.bloom.intensity});
    add({K::BloomThreshold, T::Float, "Threshold", 9, 0, 16, 0, 2, d.bloom.threshold});
    add({K::BloomKnee, T::Float, "Soft Knee", 10, 0.001, 4, 0.01, 1, d.bloom.knee});
    add({K::BloomRadius, T::Float, "Radius (% of height)", 11, 0.05, 25, 0.5, 15, d.bloom.radius, 1});
    add({K::BloomGroupEnd, T::GroupEnd, "", 12});

    s = {K::HalGroup, T::GroupStart, "Halation", 13}; s.startCollapsed = true; add(s);
    s = {K::HalEnable, T::Checkbox, "Halation Enable", 14}; s.dflt = d.halation.enabled; add(s);
    add({K::HalIntensity, T::Float, "Intensity", 15, 0, 1, 0, 1, d.halation.intensity});
    add({K::HalThreshold, T::Float, "Threshold", 16, 0, 16, 0, 2, d.halation.threshold});
    add({K::HalRadius, T::Float, "Radius (% of height)", 17, 0.05, 10, 0.1, 3, d.halation.radius});
    add({K::HalWarmth, T::Float, "Warmth (red to orange)", 18, 0, 1, 0, 1, d.halation.warmth});
    add({K::HalGroupEnd, T::GroupEnd, "", 19});

    s = {K::GrainGroup, T::GroupStart, "Film Grain", 20}; s.startCollapsed = true; add(s);
    s = {K::GrainEnable, T::Checkbox, "Grain Enable", 21}; s.dflt = d.grain.enabled; add(s);
    add({K::GrainAmount, T::Float, "Amount", 22, 0, 1, 0, 1, d.grain.amount});
    add({K::GrainSize, T::Float, "Size (px @1080)", 23, 0.5, 8, 0.5, 4, d.grain.size});
    add({K::GrainRoughness, T::Float, "Roughness", 24, 0, 1, 0, 1, d.grain.roughness});
    add({K::GrainShadows, T::Float, "Shadow Response", 25, 0, 2, 0, 2, d.grain.shadows});
    add({K::GrainMidtones, T::Float, "Midtone Response", 26, 0, 2, 0, 2, d.grain.midtones});
    add({K::GrainHighlights, T::Float, "Highlight Response", 27, 0, 2, 0, 2, d.grain.highlights});
    s = {K::GrainColor, T::Popup, "Color Mode", 28};
    s.items = "Luma (neutral)|Color"; s.itemCount = 2; s.dflt = 1; add(s);
    add({K::GrainChroma, T::Float, "Color Amount", 29, 0, 1, 0, 1, d.grain.chroma});
    add({K::GrainSeed, T::IntSlider, "Seed", 30, 0, 99999, 0, 999, double(d.grain.seed), 0});
    s = {K::GrainAnimate, T::Checkbox, "Animate", 31}; s.dflt = d.grain.animate; add(s);
    add({K::GrainGroupEnd, T::GroupEnd, "", 32});

    s = {K::GateGroup, T::GroupStart, "Film Gate", 33}; s.startCollapsed = true; add(s);
    s = {K::GateEnable, T::Checkbox, "Gate Enable", 34}; s.dflt = d.gate.enabled; add(s);
    add({K::GateWeave, T::Float, "Gate Weave (px @1080)", 35, 0, 10, 0, 4, d.gate.weave});
    add({K::GateFlicker, T::Float, "Flicker", 36, 0, 1, 0, 1, d.gate.flicker});
    add({K::GateEdge, T::Float, "Gate Edge (% of height)", 37, 0, 15, 0, 8, d.gate.edge, 1});
    add({K::GateEdgeSoftness, T::Float, "Edge Softness", 38, 0, 1, 0, 1, d.gate.edgeSoftness});
    add({K::GateEdgeRoundness, T::Float, "Corner Roundness", 39, 0, 1, 0, 1, d.gate.edgeRoundness});
    add({K::GateDust, T::Float, "Dust", 40, 0, 1, 0, 1, d.gate.dust});
    add({K::GateScratches, T::Float, "Scratches", 41, 0, 1, 0, 1, d.gate.scratches});
    s = {K::GateLook, T::Popup, "Dust & Scratch Look", 42};
    s.items = "Dark (print)|Light (negative)|Mixed"; s.itemCount = 3; s.dflt = 1; add(s);
    add({K::GateSeed, T::IntSlider, "Gate Seed", 43, 0, 99999, 0, 999, double(d.gate.seed), 0});
    add({K::GateGroupEnd, T::GroupEnd, "", 44});
    return v;
  }();
  return specs;
}

inline const ParamSpec& specFor(ParamKey k) {
  for (const auto& s : allParams())
    if (s.key == static_cast<int>(k)) return s;
  return allParams().front();  // unreachable: every key is in the table (tested)
}

/** Host parameter index of `key` (input layer is 0). */
inline int paramIndex(ParamKey key) { return vv::hostIndex(allParams(), static_cast<int>(key)); }

/** Read the parameters into core FilmParams. */
inline FilmParams readParams(const ParamReader& r) {
  auto num = [&](ParamKey k) { return r.number(paramIndex(k)); };
  auto flag = [&](ParamKey k) { return r.flag(paramIndex(k)); };
  auto pop = [&](ParamKey k) { return r.popup(paramIndex(k)); };
  using K = ParamKey;
  FilmParams p;
  p.masterEnable = flag(K::MasterEnable);
  p.mix = static_cast<float>(num(K::Mix) / 100.0);
  p.space = static_cast<WorkingSpace>(pop(K::WorkingSpace) - 1);
  p.quality = pop(K::Quality) == 2 ? Quality::High : Quality::Normal;
  p.bloom.enabled = flag(K::BloomEnable);
  p.bloom.intensity = static_cast<float>(num(K::BloomIntensity));
  p.bloom.threshold = static_cast<float>(num(K::BloomThreshold));
  p.bloom.knee = static_cast<float>(num(K::BloomKnee));
  p.bloom.radius = static_cast<float>(num(K::BloomRadius));
  p.halation.enabled = flag(K::HalEnable);
  p.halation.intensity = static_cast<float>(num(K::HalIntensity));
  p.halation.threshold = static_cast<float>(num(K::HalThreshold));
  p.halation.radius = static_cast<float>(num(K::HalRadius));
  p.halation.warmth = static_cast<float>(num(K::HalWarmth));
  p.grain.enabled = flag(K::GrainEnable);
  p.grain.amount = static_cast<float>(num(K::GrainAmount));
  p.grain.size = static_cast<float>(num(K::GrainSize));
  p.grain.roughness = static_cast<float>(num(K::GrainRoughness));
  p.grain.shadows = static_cast<float>(num(K::GrainShadows));
  p.grain.midtones = static_cast<float>(num(K::GrainMidtones));
  p.grain.highlights = static_cast<float>(num(K::GrainHighlights));
  p.grain.color = pop(K::GrainColor) == 2 ? GrainColor::Color : GrainColor::Luma;
  p.grain.chroma = static_cast<float>(num(K::GrainChroma));
  p.grain.seed = static_cast<int32_t>(num(K::GrainSeed));
  p.grain.animate = flag(K::GrainAnimate);
  p.gate.enabled = flag(K::GateEnable);
  p.gate.weave = static_cast<float>(num(K::GateWeave));
  p.gate.flicker = static_cast<float>(num(K::GateFlicker));
  p.gate.edge = static_cast<float>(num(K::GateEdge));
  p.gate.edgeSoftness = static_cast<float>(num(K::GateEdgeSoftness));
  p.gate.edgeRoundness = static_cast<float>(num(K::GateEdgeRoundness));
  p.gate.dust = static_cast<float>(num(K::GateDust));
  p.gate.scratches = static_cast<float>(num(K::GateScratches));
  p.gate.look = static_cast<DefectLook>(pop(K::GateLook) - 1);
  p.gate.seed = static_cast<int32_t>(num(K::GateSeed));
  return sanitized(p);
}

/** Preset name for a 1-based "Preset" menu value; nullptr for "(choose to apply)" or out of range. */
inline const char* presetMenuName(int value1Based) {
  static const char* kNames[] = {nullptr,           "Subtle Film",       "Soft Highlight Glow", "Night Lights",
                                 "Fine 35mm Grain", "Coarse 16mm Grain", "Halation Only",       "Off",
                                 "Old Projector"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == FE_PRESET_COUNT, "preset menu and names out of sync");
  return value1Based >= 1 && value1Based <= FE_PRESET_COUNT ? kNames[value1Based - 1] : nullptr;
}

/** Every control a preset sets, as (key, value): numbers as is, checkboxes 0/1, popups 1-based.
 *  Host adapters write these into their parameters, so presets behave the same everywhere. */
template <typename F>
inline void forEachPresetValue(const FilmParams& p, F&& set) {
  using K = ParamKey;
  set(K::BloomEnable, p.bloom.enabled ? 1.0 : 0.0);
  set(K::BloomIntensity, p.bloom.intensity);
  set(K::BloomThreshold, p.bloom.threshold);
  set(K::BloomKnee, p.bloom.knee);
  set(K::BloomRadius, p.bloom.radius);
  set(K::HalEnable, p.halation.enabled ? 1.0 : 0.0);
  set(K::HalIntensity, p.halation.intensity);
  set(K::HalThreshold, p.halation.threshold);
  set(K::HalRadius, p.halation.radius);
  set(K::HalWarmth, p.halation.warmth);
  set(K::GrainEnable, p.grain.enabled ? 1.0 : 0.0);
  set(K::GrainAmount, p.grain.amount);
  set(K::GrainSize, p.grain.size);
  set(K::GrainRoughness, p.grain.roughness);
  set(K::GrainShadows, p.grain.shadows);
  set(K::GrainMidtones, p.grain.midtones);
  set(K::GrainHighlights, p.grain.highlights);
  set(K::GrainColor, p.grain.color == GrainColor::Color ? 2.0 : 1.0);
  set(K::GrainChroma, p.grain.chroma);
  set(K::GateEnable, p.gate.enabled ? 1.0 : 0.0);
  set(K::GateWeave, p.gate.weave);
  set(K::GateFlicker, p.gate.flicker);
  set(K::GateEdge, p.gate.edge);
  set(K::GateEdgeSoftness, p.gate.edgeSoftness);
  set(K::GateEdgeRoundness, p.gate.edgeRoundness);
  set(K::GateDust, p.gate.dust);
  set(K::GateScratches, p.gate.scratches);
  set(K::GateLook, static_cast<double>(static_cast<int>(p.gate.look) + 1));
}

}  // namespace fe
