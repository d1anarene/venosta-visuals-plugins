#include "params.h"

#include <algorithm>
#include <cmath>

namespace fe {
namespace {
float clampf(float v, float lo, float hi, float fallback) {
  if (!std::isfinite(v)) return fallback;
  return std::min(std::max(v, lo), hi);
}
}  // namespace

FilmParams sanitized(const FilmParams& in) {
  FilmParams d;  // defaults for NaN replacement
  FilmParams p = in;
  p.mix = clampf(p.mix, 0.f, 1.f, d.mix);
  p.bloom.intensity = clampf(p.bloom.intensity, 0.f, 1.f, d.bloom.intensity);
  p.bloom.threshold = clampf(p.bloom.threshold, 0.f, 16.f, d.bloom.threshold);
  p.bloom.knee = clampf(p.bloom.knee, 0.001f, 4.f, d.bloom.knee);
  p.bloom.radius = clampf(p.bloom.radius, 0.05f, 25.f, d.bloom.radius);
  p.halation.intensity = clampf(p.halation.intensity, 0.f, 1.f, d.halation.intensity);
  p.halation.threshold = clampf(p.halation.threshold, 0.f, 16.f, d.halation.threshold);
  p.halation.knee = clampf(p.halation.knee, 0.001f, 4.f, d.halation.knee);
  p.halation.radius = clampf(p.halation.radius, 0.05f, 10.f, d.halation.radius);
  p.halation.warmth = clampf(p.halation.warmth, 0.f, 1.f, d.halation.warmth);
  p.grain.amount = clampf(p.grain.amount, 0.f, 1.f, d.grain.amount);
  p.grain.size = clampf(p.grain.size, 0.5f, 8.f, d.grain.size);
  p.grain.roughness = clampf(p.grain.roughness, 0.f, 1.f, d.grain.roughness);
  p.grain.shadows = clampf(p.grain.shadows, 0.f, 2.f, d.grain.shadows);
  p.grain.midtones = clampf(p.grain.midtones, 0.f, 2.f, d.grain.midtones);
  p.grain.highlights = clampf(p.grain.highlights, 0.f, 2.f, d.grain.highlights);
  p.grain.chroma = clampf(p.grain.chroma, 0.f, 1.f, d.grain.chroma);
  p.gate.weave = clampf(p.gate.weave, 0.f, 10.f, d.gate.weave);
  p.gate.flicker = clampf(p.gate.flicker, 0.f, 1.f, d.gate.flicker);
  p.gate.edge = clampf(p.gate.edge, 0.f, 15.f, d.gate.edge);
  p.gate.edgeSoftness = clampf(p.gate.edgeSoftness, 0.f, 1.f, d.gate.edgeSoftness);
  p.gate.edgeRoundness = clampf(p.gate.edgeRoundness, 0.f, 1.f, d.gate.edgeRoundness);
  p.gate.dust = clampf(p.gate.dust, 0.f, 1.f, d.gate.dust);
  p.gate.scratches = clampf(p.gate.scratches, 0.f, 1.f, d.gate.scratches);
  if (static_cast<int>(p.gate.look) < 0 || static_cast<int>(p.gate.look) > 2) p.gate.look = d.gate.look;
  return p;
}

const std::vector<Preset>& builtinPresets() {
  static const std::vector<Preset> presets = [] {
    std::vector<Preset> v;
    auto add = [&](const char* name, auto&& fn) {
      FilmParams p;
      fn(p);
      v.push_back({name, p});
    };
    add("Subtle Film", [](FilmParams& p) {
      p.bloom = {true, 0.10f, 0.85f, 0.50f, 3.5f};
      p.halation = {true, 0.08f, 0.95f, 0.30f, 0.5f, 0.35f};
      p.grain.enabled = true;
      p.grain.amount = 0.18f;
      p.grain.size = 1.2f;
    });
    add("Soft Highlight Glow", [](FilmParams& p) {
      p.bloom = {true, 0.28f, 0.70f, 0.60f, 6.0f};
    });
    add("Night Lights", [](FilmParams& p) {
      p.bloom = {true, 0.22f, 0.60f, 0.40f, 5.0f};
      p.halation = {true, 0.22f, 0.75f, 0.25f, 0.8f, 0.45f};
      p.grain.enabled = true;
      p.grain.amount = 0.22f;
      p.grain.shadows = 0.8f;
    });
    add("Fine 35mm Grain", [](FilmParams& p) {
      p.grain.enabled = true;
      p.grain.amount = 0.20f;
      p.grain.size = 1.0f;
      p.grain.roughness = 0.45f;
    });
    add("Coarse 16mm Grain", [](FilmParams& p) {
      p.grain.enabled = true;
      p.grain.amount = 0.38f;
      p.grain.size = 2.4f;
      p.grain.roughness = 0.65f;
      p.grain.shadows = 0.8f;
      p.grain.color = GrainColor::Color;
      p.grain.chroma = 0.2f;
    });
    add("Halation Only", [](FilmParams& p) {
      p.halation = {true, 0.18f, 0.85f, 0.30f, 0.7f, 0.35f};
    });
    add("Off", [](FilmParams&) {});
    // Appended last: preset menu values are saved in projects, so the order must never change.
    add("Old Projector", [](FilmParams& p) {
      p.bloom = {true, 0.12f, 0.80f, 0.50f, 4.0f};
      p.halation = {true, 0.10f, 0.90f, 0.30f, 0.6f, 0.35f};
      p.grain.enabled = true;
      p.grain.amount = 0.32f;
      p.grain.size = 1.8f;
      p.gate.enabled = true;
      p.gate.weave = 1.6f;
      p.gate.flicker = 0.35f;
      p.gate.edge = 2.5f;
      p.gate.edgeSoftness = 0.35f;
      p.gate.edgeRoundness = 0.35f;
      p.gate.dust = 0.35f;
      p.gate.scratches = 0.3f;
      p.gate.look = DefectLook::Dark;
    });
    return v;
  }();
  return presets;
}

const Preset* findPreset(const std::string& name) {
  for (const auto& p : builtinPresets())
    if (p.name == name) return &p;
  return nullptr;
}

}  // namespace fe
