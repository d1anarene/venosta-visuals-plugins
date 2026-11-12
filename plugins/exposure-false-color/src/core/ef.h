// Exposure & False Color — parameters and CPU reference (one per-pixel pass, ef_kernels.h).
#pragma once
#include <string>
#include <vector>

#include "ef_kernels.h"
#include "vv/image.h"

namespace ef {

using vv::Image;

struct Params {
  bool masterEnable = true;
  float opacity = 1.0f;     // 0..1
  int space = 0;            // 0 Rec.709 / gamma 2.4, 1 sRGB, 2 Linear
  bool legend = true;
  bool falseColor = true;
  float middleGrey = 0.18f;
  bool zebra = false;
  float zebraLevel = 0.95f;
  bool zebra2 = false;
  float zebra2Low = 0.70f, zebra2High = 0.80f;
  float stripe = 8.0f;      // px of a 1080-line frame
  bool clipping = false;
  bool split = false;
  float splitPos = 0.5f;
  bool splitHorizontal = false;
  bool splitOverlayFirst = false;  // false: overlays on the right / bottom part

  bool anyActive() const {
    return masterEnable && opacity > 0.f && (legend || falseColor || zebra || zebra2 || clipping);
  }
};

Params sanitized(const Params& p);
/** Kernel parameters for a w x h frame. */
EfInfo info(const Params& p, int width, int height);
/** CPU reference: straight-alpha RGBA float, top row first, working encoding; in place. */
bool process(Image& img, const Params& p);

struct Preset {
  std::string name;
  Params params;
};
const std::vector<Preset>& builtinPresets();
const Preset* findPreset(const std::string& name);

}  // namespace ef
