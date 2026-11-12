// Exposure & False Color — the whole effect is one per-pixel function, shared verbatim by the CPU
// reference, the CUDA kernel and the Metal kernel (valid C++17, CUDA C++ and MSL). Even the
// parameter struct is defined only here, so CPU/GPU layouts cannot drift apart.
#ifndef EF_KERNELS_H
#define EF_KERNELS_H

#include "vv/kernels.h"

// All members 4 bytes: identical layout in C++, CUDA and Metal.
struct EfInfo {
  unsigned int width, height;
  int space;                        // VV_SPACE_* of the host frame's working encoding
  int falseColor, legend, zebra, zebra2, clipping;
  int split, splitHorizontal, splitOverlayFirst;  // overlays on the left/top part when 1
  float middleGrey;                 // linear value treated as 0 stops (default 0.18)
  float zebraLevel, zebra2Low, zebra2High;  // encoded luma, 0..1.09
  float stripePx;                   // zebra stripe period in pixels
  float splitPos;                   // 0..1 across the frame
  float opacity;                    // 0..1
};

// False-colour bands (stops relative to middle grey) and their colours (Rec.709 / gamma 2.4).
#define EF_STOPS_WHITE 2.4739f  // log2(1 / 0.18): diffuse white

// Encoded colour for exposure `stops`; `grey` is the encoded luma used for the neutral ramps.
VV_FN vv_float3 ef_false_color_709(float stops, float grey, float whiteStops) {
  if (stops < -6.5f) return vv_make3(0.45f, 0.05f, 0.55f);  // crushed
  if (stops < -4.5f) return vv_make3(0.05f, 0.20f, 0.85f);  // deep shadows
  if (stops < -2.5f) return vv_make3(0.00f, 0.45f, 0.50f);  // shadows
  if (stops < -0.3f) return vv_make3(grey, grey, grey);      // shadows to mids: neutral
  if (stops < 0.3f) return vv_make3(0.10f, 0.75f, 0.20f);   // middle grey +-0.3 stop
  if (stops < 0.8f) return vv_make3(grey, grey, grey);
  if (stops < 1.4f) return vv_make3(0.95f, 0.45f, 0.70f);   // skin highlight
  if (stops < whiteStops - 0.45f) return vv_make3(grey, grey, grey);
  if (stops < whiteStops) return vv_make3(0.95f, 0.85f, 0.10f);  // near white
  return vv_make3(0.90f, 0.05f, 0.05f);                     // at / above diffuse white
}

// Rec.709 / gamma 2.4 colour into the frame's working encoding.
VV_FN vv_float3 ef_to_space(vv_float3 c709, int space) {
  if (space == VV_SPACE_REC709_G24) return c709;
  return vv_from_linear(vv_to_linear(c709, VV_SPACE_REC709_G24), space);
}

// One output pixel. enc: the pixel in the working encoding; (x, ty): pixel, top row = 0.
// Returns the overlay colour in the working encoding (before opacity).
VV_FN vv_float3 ef_shade(vv_float3 enc, float x, float ty, EfInfo i) {
  const float w = float(i.width), h = float(i.height);
  // Legend: a bar across the bottom, 60 % of the width, stops -7 .. +3.5.
  if (i.legend != 0) {
    const float bh = vv_max(4.0f, 0.025f * h), bw = 0.6f * w, bx = 0.2f * w, by = h - 2.0f * bh;
    if (ty >= by && ty < by + bh && x >= bx && x < bx + bw) {
      const float s = -7.0f + 10.5f * ((x + 0.5f - bx) / bw);
      const float g = vv_clamp(0.18f * vv_pow(2.0f, s), 0.0f, 1.0f);
      const float gEnc = vv_from_linear1(g, VV_SPACE_REC709_G24);
      return ef_to_space(ef_false_color_709(s, gEnc, EF_STOPS_WHITE), i.space);
    }
  }
  // Divider line of the split (always drawn when the split is on).
  if (i.split != 0) {
    const float pos = i.splitPos * (i.splitHorizontal != 0 ? h : w);
    const float c = (i.splitHorizontal != 0 ? ty : x) + 0.5f;
    const float lineHalf = vv_max(1.0f, h / 1080.0f);
    if (vv_fabs(c - pos) <= lineHalf) return ef_to_space(vv_make3(1.0f, 1.0f, 1.0f), i.space);
    const bool first = c < pos;
    if (first != (i.splitOverlayFirst != 0)) return enc;  // real image on this side
  }
  // Signal values: encoded luma (zebras, clipping, grey ramps) in Rec.709 terms.
  const vv_float3 lin = vv_to_linear(enc, i.space);
  const vv_float3 e709 = i.space == VV_SPACE_REC709_G24 ? enc : vv_from_linear(lin, VV_SPACE_REC709_G24);
  const float y709 = vv_luma(e709);
  vv_float3 out = enc;
  bool changed = false;
  if (i.falseColor != 0) {
    const float lum = vv_max(vv_luma(lin), 1e-7f);
    const float stops = vv_log2(lum / vv_max(i.middleGrey, 1e-4f));
    const float whiteStops = vv_log2(1.0f / vv_max(i.middleGrey, 1e-4f));
    out = ef_to_space(ef_false_color_709(stops, vv_clamp(y709, 0.0f, 1.0f), whiteStops), i.space);
    changed = true;
  }
  // Zebras: 45-degree stripes (second zebra in the other direction).
  const float period = vv_max(i.stripePx, 2.0f);
  if (i.zebra != 0 && y709 >= i.zebraLevel) {
    const float t = (x + ty) / period;
    if (t - vv_floor(t) < 0.5f) {
      out = ef_to_space(vv_make3(1.0f, 1.0f, 1.0f), i.space);
      changed = true;
    }
  } else if (i.zebra2 != 0 && y709 >= i.zebra2Low && y709 <= i.zebra2High) {
    const float t = (x - ty) / period;
    if (t - vv_floor(t) < 0.5f) {
      out = ef_to_space(vv_make3(0.55f, 0.55f, 0.55f), i.space);
      changed = true;
    }
  }
  if (i.clipping != 0) {
    const float hi = vv_max(e709.x, vv_max(e709.y, e709.z)), lo = vv_min(e709.x, vv_min(e709.y, e709.z));
    if (hi >= 1.0f) return ef_to_space(vv_make3(1.0f, 0.0f, 0.0f), i.space);
    if (lo <= 0.0f) return ef_to_space(vv_make3(0.0f, 0.2f, 1.0f), i.space);
  }
  return changed ? out : enc;
}

#endif  // EF_KERNELS_H
