// Venosta Visuals engine — per-pixel maths shared by every plugin's C++ CPU reference, CUDA
// kernels and Metal kernels.
//
// This header is valid C++17, CUDA C++ and Metal Shading Language, so CPU tests exercise exactly
// the formulas the GPU runs. Keep it free of std::, pointers, recursion and double precision.
#ifndef VV_KERNELS_H
#define VV_KERNELS_H

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
  #define VV_FN static inline
  typedef float3 vv_float3;
  typedef uint vv_uint;
  VV_FN vv_float3 vv_make3(float x, float y, float z) { return float3(x, y, z); }
  VV_FN float vv_pow(float a, float b) { return pow(a, b); }
  VV_FN float vv_exp(float a) { return exp(a); }
  VV_FN float vv_floor(float a) { return floor(a); }
  VV_FN float vv_fabs(float a) { return fabs(a); }
  VV_FN float vv_sqrt(float a) { return sqrt(a); }
#else
  #include <cmath>
  #include <cstdint>
  #ifdef __CUDACC__
    // CUDA: every function is callable from host and device; float math via the CUDA built-ins.
    #define VV_FN __host__ __device__ inline
    #define VV_POWF powf
    #define VV_EXPF expf
    #define VV_FLOORF floorf
    #define VV_FABSF fabsf
    #define VV_SQRTF sqrtf
  #else
    #define VV_FN inline
    #define VV_POWF std::pow
    #define VV_EXPF std::exp
    #define VV_FLOORF std::floor
    #define VV_FABSF std::fabs
    #define VV_SQRTF std::sqrt
  #endif
  typedef uint32_t vv_uint;
  struct vv_float3 {
    float x, y, z;
  };
  VV_FN vv_float3 vv_make3(float x, float y, float z) { return vv_float3{x, y, z}; }
  VV_FN vv_float3 operator+(vv_float3 a, vv_float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
  VV_FN vv_float3 operator-(vv_float3 a, vv_float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  VV_FN vv_float3 operator*(vv_float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
  VV_FN vv_float3 operator*(vv_float3 a, vv_float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
  VV_FN float vv_pow(float a, float b) { return VV_POWF(a, b); }
  VV_FN float vv_exp(float a) { return VV_EXPF(a); }
  VV_FN float vv_floor(float a) { return VV_FLOORF(a); }
  VV_FN float vv_fabs(float a) { return VV_FABSF(a); }
  VV_FN float vv_sqrt(float a) { return VV_SQRTF(a); }
#endif

VV_FN float vv_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
VV_FN float vv_max(float a, float b) { return a > b ? a : b; }
VV_FN float vv_min(float a, float b) { return a < b ? a : b; }
VV_FN float vv_lerp(float a, float b, float t) { return a + (b - a) * t; }
VV_FN float vv_smoothstep(float e0, float e1, float x) {
  float t = vv_clamp((x - e0) / vv_max(e1 - e0, 1e-6f), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------------------------
// Colour: Rec.709 luminance and transfer functions. Sign-preserving so negative values (from
// grading or wide-gamut conversions) pass through without NaNs.
// ---------------------------------------------------------------------------------------------
VV_FN float vv_luma(vv_float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// Working-space / transfer codes (plugins map their own enums to these).
#define VV_SPACE_REC709_G24 0  // display-referred Rec.709 / BT.1886 (gamma 2.4) — Premiere SDR default
#define VV_SPACE_SRGB 1        // sRGB piecewise curve
#define VV_SPACE_LINEAR 2      // already scene/linear light

VV_FN float vv_to_linear1(float v, int space) {
  float a = vv_fabs(v);
  float s = v < 0.0f ? -1.0f : 1.0f;
  if (space == VV_SPACE_LINEAR) return v;
  if (space == VV_SPACE_SRGB) {
    float l = a <= 0.04045f ? a / 12.92f : vv_pow((a + 0.055f) / 1.055f, 2.4f);
    return s * l;
  }
  return s * vv_pow(a, 2.4f);
}

VV_FN float vv_from_linear1(float v, int space) {
  float a = vv_fabs(v);
  float s = v < 0.0f ? -1.0f : 1.0f;
  if (space == VV_SPACE_LINEAR) return v;
  if (space == VV_SPACE_SRGB) {
    float e = a <= 0.0031308f ? a * 12.92f : 1.055f * vv_pow(a, 1.0f / 2.4f) - 0.055f;
    return s * e;
  }
  return s * vv_pow(a, 1.0f / 2.4f);
}

VV_FN vv_float3 vv_to_linear(vv_float3 c, int space) {
  return vv_make3(vv_to_linear1(c.x, space), vv_to_linear1(c.y, space), vv_to_linear1(c.z, space));
}
VV_FN vv_float3 vv_from_linear(vv_float3 c, int space) {
  return vv_make3(vv_from_linear1(c.x, space), vv_from_linear1(c.y, space), vv_from_linear1(c.z, space));
}


// ---------------------------------------------------------------------------------------------
// Deterministic hashing and noise. Integer hashing only (no RNG state), so any pixel of any frame
// can be rendered in any order, on any thread, on CPU or GPU, with identical results.
// ---------------------------------------------------------------------------------------------
VV_FN vv_uint vv_hash(vv_uint x) {
  // lowbias32 (Chris Wellons), public domain
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

VV_FN vv_uint vv_hash3(vv_uint a, vv_uint b, vv_uint c) {
  return vv_hash(a ^ vv_hash(b ^ vv_hash(c)));
}

// Approximately standard-normal value for an integer lattice point (sum of 4 uniforms, Irwin-Hall,
// rescaled to unit variance). Bounded to about ±3.46, which avoids rare "hot" pixels.
VV_FN float vv_lattice_gauss(int ix, int iy, vv_uint key) {
  vv_uint h = vv_hash3((vv_uint)ix, (vv_uint)iy, key);
  float s = 0.0f;
  for (int i = 0; i < 4; i++) {
    s += (float)(h & 0xFFFFU) * (1.0f / 65535.0f);
    h = vv_hash(h + 0x9E3779B9U);
  }
  return (s - 2.0f) * 1.7320508f;
}

VV_FN float vv_quintic(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Smoothly interpolated lattice noise ("value noise") at continuous position p in lattice units.
// The lattice is generated by hashing, so there is no repeating tile anywhere.
VV_FN float vv_value_noise(float px, float py, vv_uint key) {
  float fx = vv_floor(px), fy = vv_floor(py);
  int ix = (int)fx, iy = (int)fy;
  float tx = vv_quintic(px - fx), ty = vv_quintic(py - fy);
  float a = vv_lattice_gauss(ix, iy, key), b = vv_lattice_gauss(ix + 1, iy, key);
  float c = vv_lattice_gauss(ix, iy + 1, key), d = vv_lattice_gauss(ix + 1, iy + 1, key);
  // Bilinear-interpolated unit-variance noise has lower variance; 1.4 restores roughly unit variance.
  return vv_lerp(vv_lerp(a, b, tx), vv_lerp(c, d, tx), ty) * 1.4f;
}


// Hash -> [0, 1).
VV_FN float vv_unit(vv_uint h) { return (float)(h >> 8) * (1.0f / 16777216.0f); }

// Catmull-Rom cubic weight for tap k (offsets -1, 0, 1, 2) at fractional position t in [0, 1).
// Sharp (interpolating) resampling; weights sum to 1.
VV_FN float vv_catmull_rom(float t, int k) {
  float t2 = t * t, t3 = t2 * t;
  if (k == 0) return 0.5f * (-t3 + 2.0f * t2 - t);
  if (k == 1) return 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
  if (k == 2) return 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
  return 0.5f * (t3 - t2);
}


// ---------------------------------------------------------------------------------------------
// Highlight extraction with a soft knee (quadratic, no hard edge). Returns the factor by which
// the linear colour is multiplied to obtain the "light that scatters". Chroma is preserved.
// threshold/knee are in linear luminance units (1.0 = diffuse white in SDR).
// ---------------------------------------------------------------------------------------------
VV_FN float vv_soft_knee_weight(float lum, float threshold, float knee) {
  float l = vv_max(lum, 0.0f);
  float k = vv_max(knee, 1e-4f);
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

#endif  // VV_KERNELS_H
