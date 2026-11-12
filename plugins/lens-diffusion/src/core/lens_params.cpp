#include "lens_params.h"

#include <algorithm>
#include <cmath>

namespace ld {
namespace {
float clampf(float v, float lo, float hi, float fallback) {
  if (!std::isfinite(v)) return fallback;
  return std::min(std::max(v, lo), hi);
}
}  // namespace

LensParams sanitized(const LensParams& in) {
  const LensParams d;
  LensParams p = in;
  p.mix = clampf(p.mix, 0.f, 1.f, d.mix);
  if (static_cast<int>(p.space) < 0 || static_cast<int>(p.space) > 2) p.space = d.space;
  p.diffusion.amount = clampf(p.diffusion.amount, 0.f, 1.f, d.diffusion.amount);
  p.diffusion.radius = clampf(p.diffusion.radius, 0.2f, 25.f, d.diffusion.radius);
  p.diffusion.halo = clampf(p.diffusion.halo, 0.f, 1.f, d.diffusion.halo);
  p.diffusion.highlightBias = clampf(p.diffusion.highlightBias, 0.f, 1.f, d.diffusion.highlightBias);
  p.diffusion.warmth = clampf(p.diffusion.warmth, -1.f, 1.f, d.diffusion.warmth);
  p.vignette.amount = clampf(p.vignette.amount, -1.f, 1.f, d.vignette.amount);
  p.vignette.size = clampf(p.vignette.size, 0.1f, 1.5f, d.vignette.size);
  p.vignette.roundness = clampf(p.vignette.roundness, 0.f, 1.f, d.vignette.roundness);
  p.vignette.feather = clampf(p.vignette.feather, 0.05f, 1.f, d.vignette.feather);
  if (static_cast<int>(p.vignette.style) < 0 || static_cast<int>(p.vignette.style) > 1) p.vignette.style = d.vignette.style;
  p.aberration.amount = clampf(p.aberration.amount, -10.f, 10.f, d.aberration.amount);
  p.aberration.edgeBias = clampf(p.aberration.edgeBias, 0.f, 1.f, d.aberration.edgeBias);
  p.streaks.intensity = clampf(p.streaks.intensity, 0.f, 1.f, d.streaks.intensity);
  p.streaks.threshold = clampf(p.streaks.threshold, 0.f, 16.f, d.streaks.threshold);
  p.streaks.knee = clampf(p.streaks.knee, 0.001f, 4.f, d.streaks.knee);
  p.streaks.length = clampf(p.streaks.length, 1.f, 100.f, d.streaks.length);
  if (static_cast<int>(p.streaks.color) < 0 || static_cast<int>(p.streaks.color) > 3) p.streaks.color = d.streaks.color;
  return p;
}

const std::vector<Preset>& builtinPresets() {
  static const std::vector<Preset> presets = [] {
    std::vector<Preset> v;
    auto add = [&](const char* name, auto&& fn) {
      LensParams p;
      fn(p);
      v.push_back({name, p});
    };
    add("Subtle Mist", [](LensParams& p) {
      p.diffusion = {true, 0.15f, 2.5f, 0.45f, 0.25f, 0.0f};
    });
    add("Strong Mist", [](LensParams& p) {
      p.diffusion = {true, 0.38f, 4.0f, 0.6f, 0.1f, 0.05f};
    });
    add("Highlight Mist", [](LensParams& p) {  // black-mist style: blacks stay deep
      p.diffusion = {true, 0.3f, 3.0f, 0.55f, 0.9f, 0.0f};
    });
    add("Vintage Lens", [](LensParams& p) {
      p.diffusion = {true, 0.22f, 3.5f, 0.5f, 0.4f, 0.25f};
      p.aberration = {true, 2.5f, 0.6f};
      p.vignette.enabled = true;
      p.vignette.amount = 0.45f;
    });
    add("Anamorphic Night", [](LensParams& p) {
      p.diffusion = {true, 0.12f, 2.5f, 0.5f, 0.85f, -0.1f};
      p.streaks = {true, 0.25f, 0.9f, 0.5f, 30.0f, StreakColor::Blue};
      p.vignette.enabled = true;
      p.vignette.amount = 0.3f;
    });
    add("Natural Vignette", [](LensParams& p) {
      p.vignette.enabled = true;
      p.vignette.amount = 0.5f;
    });
    add("Off", [](LensParams&) {});
    return v;
  }();
  return presets;
}

const Preset* findPreset(const std::string& name) {
  for (const auto& p : builtinPresets())
    if (p.name == name) return &p;
  return nullptr;
}

}  // namespace ld
