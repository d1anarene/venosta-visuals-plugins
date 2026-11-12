// Exposure & False Color parameter table for the shared host adapters. Disk IDs are saved in
// projects: append only, never renumber. Host index = table position + 1.
#pragma once
#include <vector>

#include "ef.h"
#include "vv/params.h"

namespace ef {

using vv::ParamKind;
using vv::ParamReader;
using vv::ParamSpec;

enum class ParamKey : int {
  Preset, MasterEnable, Opacity, WorkingSpace, Legend,
  FcGroup, FcEnable, MiddleGrey, FcGroupEnd,
  ZbGroup, ZbEnable, ZbLevel, Zb2Enable, Zb2Low, Zb2High, ZbWidth, ZbGroupEnd,
  ClipGroup, ClipEnable, ClipGroupEnd,
  SplitGroup, SplitEnable, SplitPos, SplitOrient, SplitSide, SplitGroupEnd,
  Count
};

#define EF_PRESET_ITEMS "(choose to apply)|False Color|Zebras 95%|Skin Zebras 70-80%|Clipping Check|Split False Color"
#define EF_PRESET_COUNT 6

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
    auto popup = [&](K key, const char* name, int id, const char* items, int count, int dflt) -> ParamSpec& {
      ParamSpec& s = add(key, T::Popup, name, id, 0, 0, 0, 0, dflt);
      s.items = items; s.itemCount = count;
      return s;
    };
    auto group = [&](K key, const char* name, int id) { add(key, T::GroupStart, name, id).startCollapsed = true; };
    ParamSpec& pre = popup(K::Preset, "Preset", 1, EF_PRESET_ITEMS, EF_PRESET_COUNT, 1);
    pre.supervise = true; pre.cannotTimeVary = true;
    add(K::MasterEnable, T::Checkbox, "Master Enable (viewing aid: turn off before export)", 2, 0, 0, 0, 0, d.masterEnable);
    add(K::Opacity, T::Float, "Opacity", 3, 0, 100, 0, 100, d.opacity * 100.0, 1).percent = true;
    popup(K::WorkingSpace, "Working Space", 4, "Rec.709 / Gamma 2.4 (SDR)|sRGB|Linear", 3, 1).cannotTimeVary = true;
    add(K::Legend, T::Checkbox, "Show Legend", 5, 0, 0, 0, 0, d.legend);
    group(K::FcGroup, "False Color", 6);
    add(K::FcEnable, T::Checkbox, "False Color Enable", 7, 0, 0, 0, 0, d.falseColor);
    add(K::MiddleGrey, T::Float, "Middle Grey (linear)", 8, 0.01, 1, 0.05, 0.5, d.middleGrey, 2);
    add(K::FcGroupEnd, T::GroupEnd, "", 9);
    group(K::ZbGroup, "Zebras", 10);
    add(K::ZbEnable, T::Checkbox, "Zebra Enable", 11, 0, 0, 0, 0, d.zebra);
    add(K::ZbLevel, T::Float, "Zebra Level (%)", 12, 0, 110, 50, 109, d.zebraLevel * 100.0, 1);
    add(K::Zb2Enable, T::Checkbox, "Second Zebra Enable", 13, 0, 0, 0, 0, d.zebra2);
    add(K::Zb2Low, T::Float, "Second Zebra From (%)", 14, 0, 110, 0, 109, d.zebra2Low * 100.0, 1);
    add(K::Zb2High, T::Float, "Second Zebra To (%)", 15, 0, 110, 0, 109, d.zebra2High * 100.0, 1);
    add(K::ZbWidth, T::Float, "Stripe Width (px @1080)", 16, 2, 64, 2, 32, d.stripe, 1);
    add(K::ZbGroupEnd, T::GroupEnd, "", 17);
    group(K::ClipGroup, "Clipping", 18);
    add(K::ClipEnable, T::Checkbox, "Show Clipping (red >= 100 %, blue <= 0 %)", 19, 0, 0, 0, 0, d.clipping);
    add(K::ClipGroupEnd, T::GroupEnd, "", 20);
    group(K::SplitGroup, "Split View", 21);
    add(K::SplitEnable, T::Checkbox, "Split Enable", 22, 0, 0, 0, 0, d.split);
    add(K::SplitPos, T::Float, "Divider Position (%)", 23, 0, 100, 0, 100, d.splitPos * 100.0, 1);
    popup(K::SplitOrient, "Divider", 24, "Vertical|Horizontal", 2, 1);
    popup(K::SplitSide, "Overlays On", 25, "Right / Bottom|Left / Top", 2, 1);
    add(K::SplitGroupEnd, T::GroupEnd, "", 26);
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
  auto pop = [&](K k) { return r.popup(paramIndex(k)); };
  Params p;
  p.masterEnable = flag(K::MasterEnable);
  p.opacity = num(K::Opacity) / 100.f;
  p.space = pop(K::WorkingSpace) - 1;
  p.legend = flag(K::Legend);
  p.falseColor = flag(K::FcEnable);
  p.middleGrey = num(K::MiddleGrey);
  p.zebra = flag(K::ZbEnable);
  p.zebraLevel = num(K::ZbLevel) / 100.f;
  p.zebra2 = flag(K::Zb2Enable);
  p.zebra2Low = num(K::Zb2Low) / 100.f;
  p.zebra2High = num(K::Zb2High) / 100.f;
  p.stripe = num(K::ZbWidth);
  p.clipping = flag(K::ClipEnable);
  p.split = flag(K::SplitEnable);
  p.splitPos = num(K::SplitPos) / 100.f;
  p.splitHorizontal = pop(K::SplitOrient) == 2;
  p.splitOverlayFirst = pop(K::SplitSide) == 2;
  return sanitized(p);
}

inline const char* presetMenuName(int value1Based) {
  static const char* kNames[] = {nullptr, "False Color", "Zebras 95%", "Skin Zebras 70-80%", "Clipping Check",
                                 "Split False Color"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == EF_PRESET_COUNT, "preset menu and names out of sync");
  return value1Based >= 1 && value1Based <= EF_PRESET_COUNT ? kNames[value1Based - 1] : nullptr;
}

/** Every control a preset sets: numbers in panel units, checkboxes 0/1, popups 1-based. */
template <typename F>
inline void forEachPresetValue(const Params& p, F&& set) {
  using K = ParamKey;
  set(K::Legend, p.legend ? 1.0 : 0.0);
  set(K::FcEnable, p.falseColor ? 1.0 : 0.0);
  set(K::ZbEnable, p.zebra ? 1.0 : 0.0);
  set(K::ZbLevel, p.zebraLevel * 100.0);
  set(K::Zb2Enable, p.zebra2 ? 1.0 : 0.0);
  set(K::Zb2Low, p.zebra2Low * 100.0);
  set(K::Zb2High, p.zebra2High * 100.0);
  set(K::ClipEnable, p.clipping ? 1.0 : 0.0);
  set(K::SplitEnable, p.split ? 1.0 : 0.0);
  set(K::SplitPos, p.splitPos * 100.0);
  set(K::SplitOrient, p.splitHorizontal ? 2.0 : 1.0);
  set(K::SplitSide, p.splitOverlayFirst ? 2.0 : 1.0);
}

}  // namespace ef
