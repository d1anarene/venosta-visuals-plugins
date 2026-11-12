// Film Effects — film-specific per-pixel maths (bloom knee, halation, grain, Film Gate), on top of
// the shared engine maths in vv/kernels.h. Valid C++17, CUDA C++ and Metal Shading Language.
#ifndef FE_KERNELS_H
#define FE_KERNELS_H

#include "vv/kernels.h"


// Halation source: bright light weighted towards what the red-sensitive layer records.
VV_FN float fe_halation_source(vv_float3 lin, float threshold, float knee) {
  float redish = 0.65f * lin.x + 0.30f * lin.y + 0.05f * lin.z;
  return redish * vv_soft_knee_weight(redish, threshold, knee);
}

// Warm halation tint from deep red (warmth 0) to orange (warmth 1). Energy normalised to luma 0.3.
VV_FN vv_float3 fe_halation_tint(float warmth) {
  float w = vv_clamp(warmth, 0.0f, 1.0f);
  vv_float3 c = vv_make3(1.0f, vv_lerp(0.10f, 0.42f, w), vv_lerp(0.02f, 0.10f, w));
  return c * (0.3f / vv_luma(c));
}

// Edge-preferring spill: blurred highlights minus 92% of the local highlight itself, so uniform
// bright areas do not fill with red while dark surroundings next to bright edges receive it.
VV_FN float fe_halation_spill(float blurredSource, float localSource) {
  return vv_max(blurredSource - 0.92f * localSource, 0.0f);
}


// Multiscale grain at full-resolution pixel position (x, y). sizePx is the granule scale in
// full-resolution output pixels; roughness 0..1 moves energy into the finer octave.
VV_FN float fe_grain_field(float x, float y, float sizePx, float roughness, vv_uint key) {
  float s = vv_max(sizePx, 0.35f);
  // Octave offsets decorrelate lattices so their grid points never line up.
  float coarse = vv_value_noise(x / (s * 2.0f) + 17.31f, y / (s * 2.0f) + 41.07f, key ^ 0xA511E9B3U);
  float mid = vv_value_noise(x / s + 3.17f, y / s + 9.71f, key ^ 0x63D83595U);
  float fine = vv_value_noise(x / (s * 0.5f) + 29.53f, y / (s * 0.5f) + 5.89f, key ^ 0x1B873593U);
  float r = vv_clamp(roughness, 0.0f, 1.0f);
  float wc = 0.45f * (1.0f - r) + 0.15f;
  float wm = 0.80f;
  float wf = 0.20f + 0.60f * r;
  float norm = 1.0f / vv_pow(wc * wc + wm * wm + wf * wf, 0.5f);
  return (coarse * wc + mid * wm + fine * wf) * norm;
}

// Luminance-dependent grain amplitude. y is display-referred luma (0..1+). Three smooth bands
// (shadows, midtones, highlights) that sum to 1 at every luminance; film grain shows most in mids.
VV_FN float fe_grain_response(float y, float shadows, float mids, float highs) {
  float v = vv_clamp(y, 0.0f, 1.0f);
  float ws = 1.0f - vv_smoothstep(0.0f, 0.45f, v);
  float wh = vv_smoothstep(0.55f, 1.0f, v);
  float wm = 1.0f - ws - wh;
  return vv_max(ws * shadows + wm * mids + wh * highs, 0.0f);
}


// ---------------------------------------------------------------------------------------------
// Film Gate (per pixel). Per-frame values (weave offset, flicker gain, scratch lines) are computed
// once per frame on the host (src/core/gate) and passed in. Coordinates are full-frame pixels,
// top row = 0; sizes are already scaled to the frame.
// ---------------------------------------------------------------------------------------------

// Visible part of the frame inside a rounded, soft-edged gate aperture (1 inside, 0 outside).
// insetPx: aperture edge distance from the frame border; softPx: half width of the soft edge;
// radiusPx: corner radius.
VV_FN float fe_gate_mask(float x, float y, float w, float h, float insetPx, float softPx, float radiusPx) {
  float hx = vv_max(w * 0.5f - insetPx, 1.0f), hy = vv_max(h * 0.5f - insetPx, 1.0f);
  float r = vv_min(radiusPx, vv_min(hx, hy));
  float qx = vv_fabs(x + 0.5f - w * 0.5f) - (hx - r);
  float qy = vv_fabs(y + 0.5f - h * 0.5f) - (hy - r);
  float ox = vv_max(qx, 0.0f), oy = vv_max(qy, 0.0f);
  float sd = vv_sqrt(ox * ox + oy * oy) + vv_min(vv_max(qx, qy), 0.0f) - r;  // signed distance
  return 1.0f - vv_smoothstep(-softPx, softPx, sd);
}

// Dust at film position (x, y): signed coverage, < 0 dark speck, > 0 light speck, 0 none.
// Specks live in a hashed grid (cellPx >= 4 * sizePx), one candidate per cell with probability
// `density`, so only the 3x3 neighbouring cells need checking. look: 0 dark, 1 light, 2 mixed.
VV_FN float fe_dust(float x, float y, float cellPx, float density, float sizePx, vv_uint key, int look) {
  int cx = (int)vv_floor(x / cellPx), cy = (int)vv_floor(y / cellPx);
  float best = 0.0f;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      vv_uint h = vv_hash3((vv_uint)(cx + i), (vv_uint)(cy + j), key);
      if (vv_unit(h) >= density) continue;
      vv_uint h1 = vv_hash(h + 0x68E31DA4U), h2 = vv_hash(h + 0xB5297A4DU), h3 = vv_hash(h + 0x1B56C4E9U);
      float u = vv_unit(h3);
      float radius = sizePx * (0.3f + 0.7f * u * u);  // mostly small specks, a few larger ones
      float px = ((float)(cx + i) + 0.15f + 0.7f * vv_unit(h1)) * cellPx;
      float py = ((float)(cy + j) + 0.15f + 0.7f * vv_unit(h2)) * cellPx;
      float dx = x + 0.5f - px, dy = y + 0.5f - py;
      // Irregular outline: the radius wobbles with low-frequency noise around the speck.
      float wob = 1.0f + 0.3f * vv_clamp(vv_value_noise(dx / (radius * 0.8f) + 7.1f, dy / (radius * 0.8f) + 3.3f, h), -1.5f, 1.5f);
      float d = vv_sqrt(dx * dx + dy * dy) / vv_max(radius * wob, 0.3f);
      float cov = (1.0f - vv_smoothstep(0.7f, 1.0f, d)) * (0.55f + 0.45f * vv_unit(vv_hash(h + 0x2545F491U)));
      float sgn = look == 0 ? -1.0f : (look == 1 ? 1.0f : (((h >> 3) & 1U) != 0U ? 1.0f : -1.0f));
      if (cov * cov > best * best) best = cov * sgn;
    }
  }
  return best;
}

// One vertical scratch at film position (x, y): coverage 0..1. x0: line position at mid height,
// slope: horizontal drift per pixel of height, widthPx: line width, strength: 0..1.
VV_FN float fe_scratch(float x, float y, float h, float x0, float slope, float widthPx, float strength, vv_uint key) {
  float lx = x0 + slope * (y - h * 0.5f) + widthPx * 1.5f * vv_value_noise(0.37f, y / (h * 0.15f), key);
  float d = vv_fabs(x + 0.5f - lx);
  float core = 1.0f - vv_smoothstep(widthPx * 0.5f, widthPx * 0.5f + 1.0f, d);
  // Breaks and density changes along the length.
  float along = vv_clamp(0.55f + 0.6f * vv_value_noise(5.3f, y / (h * 0.04f), key ^ 0x9E3779B9U), 0.0f, 1.0f);
  return core * along * strength;
}

// Apply defects (signed: dark < 0 < light), flicker gain and gate mask to an encoded colour.
VV_FN vv_float3 fe_gate_shade(vv_float3 enc, float defect, float gain, float mask, int space) {
  if (defect == 0.0f && gain == 1.0f && mask == 1.0f) return enc;  // nothing to do: exact
  vv_float3 l = vv_to_linear(enc, space);
  if (defect < 0.0f) {
    l = l * (1.0f + 0.9f * defect);  // dark: dust on the print blocks light
  } else if (defect > 0.0f) {
    vv_float3 up = vv_make3(vv_max(1.0f - l.x, 0.0f), vv_max(1.0f - l.y, 0.0f), vv_max(1.0f - l.z, 0.0f));
    l = l + up * (0.9f * defect);    // light: dust on the negative prints as white
  }
  l = l * (gain * mask);
  return vv_from_linear(l, space);
}

#endif  // FE_KERNELS_H
