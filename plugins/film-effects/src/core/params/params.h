// Film Effects — parameter model shared by every host adapter and the CLI.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fe {

enum class WorkingSpace : int { Rec709Gamma24 = 0, SRGB = 1, Linear = 2 };
enum class GrainColor : int { Luma = 0, Color = 1 };
enum class Quality : int { Normal = 0, High = 1 };

struct BloomParams {
  bool enabled = false;
  float intensity = 0.15f;  // 0..1, fraction of scattered highlight light added back
  float threshold = 0.80f;  // linear luminance where bloom starts (1.0 = SDR diffuse white)
  float knee = 0.50f;       // soft-knee half width, linear luminance
  float radius = 4.0f;      // % of frame height (largest Gaussian sigma)
};

struct HalationParams {
  bool enabled = false;
  float intensity = 0.10f;  // 0..1
  float threshold = 0.90f;  // linear "red-layer" luminance
  float knee = 0.30f;
  float radius = 0.6f;      // % of frame height
  float warmth = 0.35f;     // 0 = deep red, 1 = orange
};

struct GrainParams {
  bool enabled = false;
  float amount = 0.25f;     // 0..1 (1 = very heavy)
  float size = 1.4f;        // granule scale in pixels of a 1080-line frame
  float roughness = 0.5f;   // 0..1: soft clusters .. crisp fine grain
  float shadows = 0.6f;     // response multipliers per tonal band
  float midtones = 1.0f;
  float highlights = 0.45f;
  GrainColor color = GrainColor::Luma;
  float chroma = 0.15f;     // colour grain strength relative to luma grain (Color mode only)
  int32_t seed = 1;
  bool animate = true;
};

struct FilmParams {
  bool masterEnable = true;
  float mix = 1.0f;  // 0..1
  WorkingSpace space = WorkingSpace::Rec709Gamma24;
  Quality quality = Quality::Normal;
  BloomParams bloom;
  HalationParams halation;
  GrainParams grain;

  bool anyEffectActive() const {
    return masterEnable && mix > 0.0f &&
           ((bloom.enabled && bloom.intensity > 0.0f) ||
            (halation.enabled && halation.intensity > 0.0f) ||
            (grain.enabled && grain.amount > 0.0f));
  }
};

/** Clamp every value to its documented range and replace NaN/Inf with defaults. */
FilmParams sanitized(const FilmParams& p);

struct Preset {
  std::string name;
  FilmParams params;
};

/** Built-in presets: Subtle Film, Soft Highlight Glow, Night Lights, Fine 35mm Grain,
 *  Coarse 16mm Grain, Halation Only, Off. */
const std::vector<Preset>& builtinPresets();
const Preset* findPreset(const std::string& name);

}  // namespace fe
