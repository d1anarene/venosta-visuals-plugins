// Film Effects — per-pixel math shared by the C++ CPU reference and the Metal kernels.
//
// This header is valid C++17, CUDA C++ and Metal Shading Language. The CPU tests therefore
// exercise exactly the same formulas the GPU runs, which is how CPU/GPU agreement is kept.
// Keep it free of std::, pointers, recursion and double precision.
#ifndef FE_KERNELS_H
#define FE_KERNELS_H

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
  #define FE_FN static inline
  typedef float3 fe_float3;
  typedef uint fe_uint;
  FE_FN fe_float3 fe_make3(float x, float y, float z) { return float3(x, y, z); }
  FE_FN float fe_pow(float a, float b) { return pow(a, b); }
  FE_FN float fe_exp(float a) { return exp(a); }
  FE_FN float fe_floor(float a) { return floor(a); }
  FE_FN float fe_fabs(float a) { return fabs(a); }
  FE_FN float fe_sqrt(float a) { return sqrt(a); }
#else
  #include <cmath>
  #include <cstdint>
  #ifdef __CUDACC__
    // CUDA: every function is callable from host and device; float math via the CUDA built-ins.
    #define FE_FN __host__ __device__ inline
    #define FE_POWF powf
    #define FE_EXPF expf
    #define FE_FLOORF floorf
    #define FE_FABSF fabsf
    #define FE_SQRTF sqrtf
  #else
    #define FE_FN inline
    #define FE_POWF std::pow
    #define FE_EXPF std::exp
    #define FE_FLOORF std::floor
    #define FE_FABSF std::fabs
    #define FE_SQRTF std::sqrt
  #endif
  typedef uint32_t fe_uint;
  struct fe_float3 {
    float x, y, z;
  };
  FE_FN fe_float3 fe_make3(float x, float y, float z) { return fe_float3{x, y, z}; }
  FE_FN fe_float3 operator+(fe_float3 a, fe_float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
  FE_FN fe_float3 operator-(fe_float3 a, fe_float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  FE_FN fe_float3 operator*(fe_float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
  FE_FN fe_float3 operator*(fe_float3 a, fe_float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
  FE_FN float fe_pow(float a, float b) { return FE_POWF(a, b); }
  FE_FN float fe_exp(float a) { return FE_EXPF(a); }
  FE_FN float fe_floor(float a) { return FE_FLOORF(a); }
  FE_FN float fe_fabs(float a) { return FE_FABSF(a); }
  FE_FN float fe_sqrt(float a) { return FE_SQRTF(a); }
#endif

FE_FN float fe_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
FE_FN float fe_max(float a, float b) { return a > b ? a : b; }
FE_FN float fe_min(float a, float b) { return a < b ? a : b; }
FE_FN float fe_lerp(float a, float b, float t) { return a + (b - a) * t; }
FE_FN float fe_smoothstep(float e0, float e1, float x) {
  float t = fe_clamp((x - e0) / fe_max(e1 - e0, 1e-6f), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------------------------
// Colour: Rec.709 luminance and transfer functions. Sign-preserving so negative values (from
// grading or wide-gamut conversions) pass through without NaNs.
// ---------------------------------------------------------------------------------------------
FE_FN float fe_luma(fe_float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// Working-space codes (must match FilmParams::WorkingSpace).
#define FE_SPACE_REC709_G24 0  // display-referred Rec.709 / BT.1886 (gamma 2.4) — Premiere SDR default
#define FE_SPACE_SRGB 1        // sRGB piecewise curve
#define FE_SPACE_LINEAR 2      // already scene/linear light

FE_FN float fe_to_linear1(float v, int space) {
  float a = fe_fabs(v);
  float s = v < 0.0f ? -1.0f : 1.0f;
  if (space == FE_SPACE_LINEAR) return v;
  if (space == FE_SPACE_SRGB) {
    float l = a <= 0.04045f ? a / 12.92f : fe_pow((a + 0.055f) / 1.055f, 2.4f);
    return s * l;
  }
  return s * fe_pow(a, 2.4f);
}

FE_FN float fe_from_linear1(float v, int space) {
  float a = fe_fabs(v);
  float s = v < 0.0f ? -1.0f : 1.0f;
  if (space == FE_SPACE_LINEAR) return v;
  if (space == FE_SPACE_SRGB) {
    float e = a <= 0.0031308f ? a * 12.92f : 1.055f * fe_pow(a, 1.0f / 2.4f) - 0.055f;
    return s * e;
  }
  return s * fe_pow(a, 1.0f / 2.4f);
}

FE_FN fe_float3 fe_to_linear(fe_float3 c, int space) {
  return fe_make3(fe_to_linear1(c.x, space), fe_to_linear1(c.y, space), fe_to_linear1(c.z, space));
}
FE_FN fe_float3 fe_from_linear(fe_float3 c, int space) {
  return fe_make3(fe_from_linear1(c.x, space), fe_from_linear1(c.y, space), fe_from_linear1(c.z, space));
}

// ---------------------------------------------------------------------------------------------
// Highlight extraction with a soft knee (quadratic, no hard edge). Returns the factor by which
// the linear colour is multiplied to obtain the "light that scatters". Chroma is preserved.
// threshold/knee are in linear luminance units (1.0 = diffuse white in SDR).
// ---------------------------------------------------------------------------------------------
FE_FN float fe_soft_knee_weight(float lum, float threshold, float knee) {
  float l = fe_max(lum, 0.0f);
  float k = fe_max(knee, 1e-4f);
  float lo = threshold - k;
  float excess;
  if (l <= lo) {
    excess = 0.0f;
  } else if (l < threshold + k) {
    float t = l - lo;
    excess = t * t / (4.0f * k);
  } else {
    excess = l - threshold;
  }
  return l > 1e-6f ? excess / l : 0.0f;
}

// Halation source: bright light weighted towards what the red-sensitive layer records.
FE_FN float fe_halation_source(fe_float3 lin, float threshold, float knee) {
  float redish = 0.65f * lin.x + 0.30f * lin.y + 0.05f * lin.z;
  return redish * fe_soft_knee_weight(redish, threshold, knee);
}

// Warm halation tint from deep red (warmth 0) to orange (warmth 1). Energy normalised to luma 0.3.
FE_FN fe_float3 fe_halation_tint(float warmth) {
  float w = fe_clamp(warmth, 0.0f, 1.0f);
  fe_float3 c = fe_make3(1.0f, fe_lerp(0.10f, 0.42f, w), fe_lerp(0.02f, 0.10f, w));
  return c * (0.3f / fe_luma(c));
}

// Edge-preferring spill: blurred highlights minus 92% of the local highlight itself, so uniform
// bright areas do not fill with red while dark surroundings next to bright edges receive it.
FE_FN float fe_halation_spill(float blurredSource, float localSource) {
  return fe_max(blurredSource - 0.92f * localSource, 0.0f);
}

// ---------------------------------------------------------------------------------------------
// Deterministic grain. Integer hashing only (no RNG state), so any pixel of any frame can be
// rendered in any order, on any thread, on CPU or GPU, with identical results.
// ---------------------------------------------------------------------------------------------
FE_FN fe_uint fe_hash(fe_uint x) {
  // lowbias32 (Chris Wellons), public domain
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

FE_FN fe_uint fe_hash3(fe_uint a, fe_uint b, fe_uint c) {
  return fe_hash(a ^ fe_hash(b ^ fe_hash(c)));
}

// Approximately standard-normal value for an integer lattice point (sum of 4 uniforms, Irwin-Hall,
// rescaled to unit variance). Bounded to about ±3.46, which avoids rare "hot" pixels.
FE_FN float fe_lattice_gauss(int ix, int iy, fe_uint key) {
  fe_uint h = fe_hash3((fe_uint)ix, (fe_uint)iy, key);
  float s = 0.0f;
  for (int i = 0; i < 4; i++) {
    s += (float)(h & 0xFFFFU) * (1.0f / 65535.0f);
    h = fe_hash(h + 0x9E3779B9U);
  }
  return (s - 2.0f) * 1.7320508f;
}

FE_FN float fe_quintic(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Smoothly interpolated lattice noise ("value noise") at continuous position p in lattice units.
// The lattice is generated by hashing, so there is no repeating tile anywhere.
FE_FN float fe_value_noise(float px, float py, fe_uint key) {
  float fx = fe_floor(px), fy = fe_floor(py);
  int ix = (int)fx, iy = (int)fy;
  float tx = fe_quintic(px - fx), ty = fe_quintic(py - fy);
  float a = fe_lattice_gauss(ix, iy, key), b = fe_lattice_gauss(ix + 1, iy, key);
  float c = fe_lattice_gauss(ix, iy + 1, key), d = fe_lattice_gauss(ix + 1, iy + 1, key);
  // Bilinear-interpolated unit-variance noise has lower variance; 1.4 restores roughly unit variance.
  return fe_lerp(fe_lerp(a, b, tx), fe_lerp(c, d, tx), ty) * 1.4f;
}

// Multiscale grain at full-resolution pixel position (x, y). sizePx is the granule scale in
// full-resolution output pixels; roughness 0..1 moves energy into the finer octave.
FE_FN float fe_grain_field(float x, float y, float sizePx, float roughness, fe_uint key) {
  float s = fe_max(sizePx, 0.35f);
  // Octave offsets decorrelate lattices so their grid points never line up.
  float coarse = fe_value_noise(x / (s * 2.0f) + 17.31f, y / (s * 2.0f) + 41.07f, key ^ 0xA511E9B3U);
  float mid = fe_value_noise(x / s + 3.17f, y / s + 9.71f, key ^ 0x63D83595U);
  float fine = fe_value_noise(x / (s * 0.5f) + 29.53f, y / (s * 0.5f) + 5.89f, key ^ 0x1B873593U);
  float r = fe_clamp(roughness, 0.0f, 1.0f);
  float wc = 0.45f * (1.0f - r) + 0.15f;
  float wm = 0.80f;
  float wf = 0.20f + 0.60f * r;
  float norm = 1.0f / fe_pow(wc * wc + wm * wm + wf * wf, 0.5f);
  return (coarse * wc + mid * wm + fine * wf) * norm;
}

// Luminance-dependent grain amplitude. y is display-referred luma (0..1+). Three smooth bands
// (shadows, midtones, highlights) that sum to 1 at every luminance; film grain shows most in mids.
FE_FN float fe_grain_response(float y, float shadows, float mids, float highs) {
  float v = fe_clamp(y, 0.0f, 1.0f);
  float ws = 1.0f - fe_smoothstep(0.0f, 0.45f, v);
  float wh = fe_smoothstep(0.55f, 1.0f, v);
  float wm = 1.0f - ws - wh;
  return fe_max(ws * shadows + wm * mids + wh * highs, 0.0f);
}

// ---------------------------------------------------------------------------------------------
// Film Gate (per pixel). Per-frame values (weave offset, flicker gain, scratch lines) are computed
// once per frame on the host (src/core/gate) and passed in. Coordinates are full-frame pixels,
// top row = 0; sizes are already scaled to the frame.
// ---------------------------------------------------------------------------------------------

// Hash -> [0, 1).
FE_FN float fe_unit(fe_uint h) { return (float)(h >> 8) * (1.0f / 16777216.0f); }

// Catmull-Rom cubic weight for tap k (offsets -1, 0, 1, 2) at fractional position t in [0, 1).
// Sharp (interpolating) resampling for the gate weave; weights sum to 1.
FE_FN float fe_catmull_rom(float t, int k) {
  float t2 = t * t, t3 = t2 * t;
  if (k == 0) return 0.5f * (-t3 + 2.0f * t2 - t);
  if (k == 1) return 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
  if (k == 2) return 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
  return 0.5f * (t3 - t2);
}

// Visible part of the frame inside a rounded, soft-edged gate aperture (1 inside, 0 outside).
// insetPx: aperture edge distance from the frame border; softPx: half width of the soft edge;
// radiusPx: corner radius.
FE_FN float fe_gate_mask(float x, float y, float w, float h, float insetPx, float softPx, float radiusPx) {
  float hx = fe_max(w * 0.5f - insetPx, 1.0f), hy = fe_max(h * 0.5f - insetPx, 1.0f);
  float r = fe_min(radiusPx, fe_min(hx, hy));
  float qx = fe_fabs(x + 0.5f - w * 0.5f) - (hx - r);
  float qy = fe_fabs(y + 0.5f - h * 0.5f) - (hy - r);
  float ox = fe_max(qx, 0.0f), oy = fe_max(qy, 0.0f);
  float sd = fe_sqrt(ox * ox + oy * oy) + fe_min(fe_max(qx, qy), 0.0f) - r;  // signed distance
  return 1.0f - fe_smoothstep(-softPx, softPx, sd);
}

// Dust at film position (x, y): signed coverage, < 0 dark speck, > 0 light speck, 0 none.
// Specks live in a hashed grid (cellPx >= 4 * sizePx), one candidate per cell with probability
// `density`, so only the 3x3 neighbouring cells need checking. look: 0 dark, 1 light, 2 mixed.
FE_FN float fe_dust(float x, float y, float cellPx, float density, float sizePx, fe_uint key, int look) {
  int cx = (int)fe_floor(x / cellPx), cy = (int)fe_floor(y / cellPx);
  float best = 0.0f;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      fe_uint h = fe_hash3((fe_uint)(cx + i), (fe_uint)(cy + j), key);
      if (fe_unit(h) >= density) continue;
      fe_uint h1 = fe_hash(h + 0x68E31DA4U), h2 = fe_hash(h + 0xB5297A4DU), h3 = fe_hash(h + 0x1B56C4E9U);
      float u = fe_unit(h3);
      float radius = sizePx * (0.3f + 0.7f * u * u);  // mostly small specks, a few larger ones
      float px = ((float)(cx + i) + 0.15f + 0.7f * fe_unit(h1)) * cellPx;
      float py = ((float)(cy + j) + 0.15f + 0.7f * fe_unit(h2)) * cellPx;
      float dx = x + 0.5f - px, dy = y + 0.5f - py;
      // Irregular outline: the radius wobbles with low-frequency noise around the speck.
      float wob = 1.0f + 0.3f * fe_clamp(fe_value_noise(dx / (radius * 0.8f) + 7.1f, dy / (radius * 0.8f) + 3.3f, h), -1.5f, 1.5f);
      float d = fe_sqrt(dx * dx + dy * dy) / fe_max(radius * wob, 0.3f);
      float cov = (1.0f - fe_smoothstep(0.7f, 1.0f, d)) * (0.55f + 0.45f * fe_unit(fe_hash(h + 0x2545F491U)));
      float sgn = look == 0 ? -1.0f : (look == 1 ? 1.0f : (((h >> 3) & 1U) != 0U ? 1.0f : -1.0f));
      if (cov * cov > best * best) best = cov * sgn;
    }
  }
  return best;
}

// One vertical scratch at film position (x, y): coverage 0..1. x0: line position at mid height,
// slope: horizontal drift per pixel of height, widthPx: line width, strength: 0..1.
FE_FN float fe_scratch(float x, float y, float h, float x0, float slope, float widthPx, float strength, fe_uint key) {
  float lx = x0 + slope * (y - h * 0.5f) + widthPx * 1.5f * fe_value_noise(0.37f, y / (h * 0.15f), key);
  float d = fe_fabs(x + 0.5f - lx);
  float core = 1.0f - fe_smoothstep(widthPx * 0.5f, widthPx * 0.5f + 1.0f, d);
  // Breaks and density changes along the length.
  float along = fe_clamp(0.55f + 0.6f * fe_value_noise(5.3f, y / (h * 0.04f), key ^ 0x9E3779B9U), 0.0f, 1.0f);
  return core * along * strength;
}

// Apply defects (signed: dark < 0 < light), flicker gain and gate mask to an encoded colour.
FE_FN fe_float3 fe_gate_shade(fe_float3 enc, float defect, float gain, float mask, int space) {
  if (defect == 0.0f && gain == 1.0f && mask == 1.0f) return enc;  // nothing to do: exact
  fe_float3 l = fe_to_linear(enc, space);
  if (defect < 0.0f) {
    l = l * (1.0f + 0.9f * defect);  // dark: dust on the print blocks light
  } else if (defect > 0.0f) {
    fe_float3 up = fe_make3(fe_max(1.0f - l.x, 0.0f), fe_max(1.0f - l.y, 0.0f), fe_max(1.0f - l.z, 0.0f));
    l = l + up * (0.9f * defect);    // light: dust on the negative prints as white
  }
  l = l * (gain * mask);
  return fe_from_linear(l, space);
}

#endif  // FE_KERNELS_H
