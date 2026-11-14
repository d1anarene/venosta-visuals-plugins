// Denoise — per-pixel maths shared verbatim by the CPU reference, CUDA and Metal (C++17, CUDA C++,
// MSL; same rules as vv/kernels.h). Noise profile lookup and the Wiener gains of the spatial stage.
#ifndef DN_KERNELS_H
#define DN_KERNELS_H

#include "vv/kernels.h"

// Pointers to small per-thread arrays: MSL needs the address space spelled out.
#ifdef __METAL_VERSION__
#define DN_THREAD thread
#else
#define DN_THREAD
#endif

#define DN_BANDS 5
#define DN_BINS 16

// Flat noise table for the GPU: sigma[band][rgb channel][bin] then corr[band][3] (RG, RB, GB).
struct DnProfileTable {
  float sigma[DN_BANDS * 3 * DN_BINS];
  float corr[DN_BANDS * 3];
};

// Interpolated between bin centres; constant beyond the first/last centre (NoiseProfile::at).
VV_FN float dn_sigma_at(DN_THREAD const float* row, float value) {
  const float t = (value == value ? value : 0.0f) * (float)DN_BINS - 0.5f;
  if (t <= 0.0f) return row[0];
  if (t >= (float)(DN_BINS - 1)) return row[DN_BINS - 1];
  const int i = (int)t;
  return row[i] + (row[i + 1] - row[i]) * (t - (float)i);
}

// Noise variance of the three opponent channels (dn_color.h) at encoded RGB `rgb` in band b:
// W S W^T with S_ij = rho_ij sigma_i sigma_j.
VV_FN vv_float3 dn_opponent_variance(DN_THREAD const float* sigmaBand, DN_THREAD const float* corrBand, vv_float3 rgb) {
  const float s0 = dn_sigma_at(sigmaBand, rgb.x), s1 = dn_sigma_at(sigmaBand + DN_BINS, rgb.y);
  const float s2 = dn_sigma_at(sigmaBand + 2 * DN_BINS, rgb.z);
  const float c01 = corrBand[0] * s0 * s1, c02 = corrBand[1] * s0 * s2, c12 = corrBand[2] * s1 * s2;
  const float v0 = s0 * s0, v1 = s1 * s1, v2 = s2 * s2;
  // Rows of W: (1,1,1)/sqrt3, (1,0,-1)/sqrt2, (1,-2,1)/sqrt6.
  const float l = (v0 + v1 + v2 + 2.0f * (c01 + c02 + c12)) * (1.0f / 3.0f);
  const float a = (v0 + v2 - 2.0f * c02) * 0.5f;
  const float b = (v0 + 4.0f * v1 + v2 - 4.0f * c01 + 2.0f * c02 - 4.0f * c12) * (1.0f / 6.0f);
  return vv_make3(vv_max(l, 0.0f), vv_max(a, 0.0f), vv_max(b, 0.0f));
}

// Stage 1 (pilot) of the empirical Wiener: squared pilot coefficient from coefficient w, local
// energy E and noise variance var (tau = pilot threshold in noise std).
VV_FN float dn_pilot_sq(float w, float E, float var, float tau) {
  const float En = vv_max(E, 1e-20f), sig = vv_max(En - tau * tau * var, 0.0f);
  const float p = w * sig / En;
  return p * p;
}
// Stage 2 gain from the smoothed pilot energy P.
VV_FN float dn_wiener_gain(float P, float var, float strength, float recovery) {
  const float denom = P + strength * var;
  float g = denom > 0.0f ? P / denom : 1.0f;
  g = g + recovery * (1.0f - g);
  return g == g ? g : 1.0f;
}

#endif  // DN_KERNELS_H
