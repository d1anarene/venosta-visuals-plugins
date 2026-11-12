// Lens & Diffusion — parameter model (host independent).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ld {

enum class WorkingSpace : int { Rec709Gamma24 = 0, SRGB = 1, Linear = 2 };  // == VV_SPACE_* codes
enum class VignetteStyle : int { Natural = 0, Smooth = 1 };
enum class StreakColor : int { Blue = 0, Cyan = 1, Neutral = 2, Amber = 3 };

struct DiffusionParams {
  bool enabled = false;
  float amount = 0.25f;        // 0..1 fraction of the (weighted) light that is scattered
  float radius = 3.0f;         // % of frame height, size of the scattered light
  float halo = 0.5f;           // 0..1 weight of the wide tail of the point-spread function
  float highlightBias = 0.5f;  // 0 = all tones scatter (mist), 1 = mostly highlights (black mist)
  float warmth = 0.0f;         // -1 cool .. +1 warm tint of the scattered light
};

struct VignetteParams {
  bool enabled = false;
  float amount = 0.35f;     // -1 (lighten edges) .. 1 (darken edges)
  float size = 0.6f;        // 0.1..1.5: where the fall-off sits (1 = frame corner)
  float roundness = 0.5f;   // 0 = follows the frame's aspect, 1 = circle
  float feather = 0.5f;     // 0.05..1 softness (Smooth style)
  VignetteStyle style = VignetteStyle::Natural;
};

struct AberrationParams {
  bool enabled = false;
  float amount = 2.0f;      // -10..10: red/blue separation at the frame corner, px of a 1080 frame
  float edgeBias = 0.5f;    // 0 = grows linearly from the centre, 1 = concentrated at the edges
};

struct StreakParams {
  bool enabled = false;
  float intensity = 0.2f;   // 0..1
  float threshold = 1.0f;   // linear luminance where streaks start
  float knee = 0.5f;
  float length = 25.0f;     // % of frame width
  StreakColor color = StreakColor::Blue;
};

struct LensParams {
  bool masterEnable = true;
  float mix = 1.0f;  // 0..1
  WorkingSpace space = WorkingSpace::Rec709Gamma24;
  DiffusionParams diffusion;
  VignetteParams vignette;
  AberrationParams aberration;
  StreakParams streaks;

  bool anyEffectActive() const {
    return masterEnable && mix > 0.0f &&
           ((diffusion.enabled && diffusion.amount > 0.0f) || (vignette.enabled && vignette.amount != 0.0f) ||
            (aberration.enabled && aberration.amount != 0.0f) || (streaks.enabled && streaks.intensity > 0.0f));
  }
};

/** Clamp every value to its documented range and replace NaN/Inf with defaults. */
LensParams sanitized(const LensParams& p);

struct Preset {
  std::string name;
  LensParams params;
};
/** Built-in presets (menu order, append only). */
const std::vector<Preset>& builtinPresets();
const Preset* findPreset(const std::string& name);

}  // namespace ld
