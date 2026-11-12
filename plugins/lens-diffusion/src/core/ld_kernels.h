// Lens & Diffusion — per-pixel maths shared by the CPU reference and the GPU kernels. Valid C++17,
// CUDA C++ and Metal Shading Language (no std::, pointers or doubles), on top of vv/kernels.h.
#ifndef LD_KERNELS_H
#define LD_KERNELS_H

#include "vv/kernels.h"

// Light that a diffusion filter scatters, as a fraction of the pixel's linear colour.
// bias 0: every tone scatters equally (mist); bias 1: mostly highlights (black-mist style), via a
// soft knee around 0.5 linear (diffuse white = 1.0).
VV_FN float ld_diffusion_weight(float lum, float bias) {
  return (1.0f - bias) + bias * vv_soft_knee_weight(lum, 0.5f, 0.4f);
}

// Colour (luma 1) for a warmth in -1 (cool) .. 1 (warm).
VV_FN vv_float3 ld_warmth_tint(float warmth) {
  vv_float3 t = vv_make3(1.0f + 0.15f * warmth, 1.0f, 1.0f - 0.15f * warmth);
  return t * (1.0f / vv_luma(t));
}

// Streak colours (luma 1): 0 blue, 1 cyan, 2 neutral, 3 amber.
VV_FN vv_float3 ld_streak_tint(int color) {
  vv_float3 t = color == 0 ? vv_make3(0.35f, 0.6f, 1.0f)
              : color == 1 ? vv_make3(0.35f, 0.9f, 1.0f)
              : color == 3 ? vv_make3(1.0f, 0.65f, 0.3f)
                           : vv_make3(1.0f, 1.0f, 1.0f);
  return t * (1.0f / vv_luma(t));
}

// Normalised distance of pixel (x, y) from the frame centre: 1 at the corners. roundness 0 follows
// the frame's aspect (elliptical), 1 is a circle in pixels.
VV_FN float ld_vignette_distance(float x, float y, float w, float h, float roundness) {
  float dx = x + 0.5f - w * 0.5f, dy = y + 0.5f - h * 0.5f;
  float u = dx / (w * 0.5f), v = dy / (h * 0.5f);
  float ell = vv_sqrt(u * u + v * v) * 0.70710678f;
  float circ = vv_sqrt(dx * dx + dy * dy) / vv_sqrt(w * w * 0.25f + h * h * 0.25f);
  return vv_lerp(ell, circ, roundness);
}

// Vignette gain (linear light). style 0: natural optical fall-off, cos^4 of the field angle with
// tan(angle) = d / size; style 1: smooth styled edge around `size` with width `feather`.
// amount > 0 darkens towards the edges, < 0 lightens.
VV_FN float ld_vignette_gain(float d, float amount, float size, float feather, int style) {
  float g;
  if (style == 0) {
    float q = d / vv_max(size, 0.05f);
    float c2 = 1.0f / (1.0f + q * q);  // cos^2
    g = c2 * c2;                       // cos^4
  } else {
    g = 1.0f - vv_smoothstep(size - feather * 0.5f, size + feather * 0.5f, d);
  }
  return 1.0f - amount * (1.0f - g);  // amount < 0 brightens by the same profile
}

// Lateral chromatic aberration: radial displacement (pixels) of a colour channel at relative
// radius rr = r / r_corner. cornerPx: displacement at the corner; bias 0 linear, 1 quadratic.
VV_FN float ld_aberration_shift(float rr, float cornerPx, float bias) {
  return cornerPx * ((1.0f - bias) * rr + bias * rr * rr);
}

#endif  // LD_KERNELS_H
