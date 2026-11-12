// Retro — parameters and CPU reference.
//
// Pipeline: encoded RGB -> Y'UV -> three horizontal-only blurs (luma softness, a wider luma for the
// ringing, chroma) -> per output pixel: geometry (CRT curvature, tape jitter/tracking/head
// switching) -> bilinear samples of the blurred signal -> rt_finish (ringing, chroma shift and
// saturation, noise, dropouts, scanlines, mask) -> mix. Alpha unchanged.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "rt_kernels.h"
#include "vv/image.h"

namespace rt {

using vv::Image;

struct Params {
  bool masterEnable = true;
  float mix = 1.0f;
  int seed = 3;
  bool animate = true;
  bool bleed = true;
  float lumaSoft = 1.2f;     // px @1080 (horizontal luma blur sigma)
  float ringing = 0.35f;     // 0..1
  float chromaBlur = 8.0f;   // px @1080
  float chromaShift = 2.0f;  // px @1080
  float saturation = 0.9f;   // 0..2
  bool tape = true;
  float jitter = 1.5f;       // px @1080
  float tracking = 0.2f;     // 0..1
  float dropouts = 0.15f;    // 0..1
  float headSwitch = 0.5f;   // 0..1
  bool noise = true;
  float lumaNoise = 0.15f, chromaNoise = 0.1f;
  bool crt = false;
  float scanlines = 0.35f;   // 0..1
  float lineCount = 480.0f;  // scanlines per frame height
  int maskType = 1;          // 0 none, 1 aperture grille, 2 shadow mask
  float maskStrength = 0.25f, curvature = 0.15f;

  bool anyActive() const { return masterEnable && mix > 0.f && (bleed || tape || noise || crt); }
};

Params sanitized(const Params& p);

/** Blur sigmas (pixels) for a frame height: luma, ringing luma, chroma; < 0.05 means none. */
struct Blurs {
  float luma, ring, chroma;
};
Blurs blurs(const Params& p, int height);

/** Per-pixel parameters for one frame (deterministic per seed and frame key). */
RtInfo info(const Params& p, int width, int height, int64_t frameKey);

/** CPU reference: straight-alpha RGBA float, top row first, encoded; in place. */
bool process(Image& img, const Params& p, int64_t frameKey);

struct Preset {
  std::string name;
  Params params;
};
const std::vector<Preset>& builtinPresets();
const Preset* findPreset(const std::string& name);

}  // namespace rt
