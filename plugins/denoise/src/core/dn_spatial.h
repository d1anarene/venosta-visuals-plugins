// Denoise — spatial candidate (a): profile-aware two-stage empirical Wiener shrinkage in the
// undecimated a trous domain (dn_starlet.h), per opponent channel (dn_color.h).
//
// For band j, opponent channel k, pixel x: sigma = noise std from the profile at the local colour
// (starlet lowpass below band j, W S W^T, dn_profile.h).
//   Stage 1 (pilot): local coefficient energy E = G * w^2 (Gaussian window); estimated signal
//     energy max(E - tau^2 sigma^2, 0); pilot coefficient p = w * max(E - tau^2 sigma^2, 0) / E.
//   Stage 2 (Wiener): P = G * p^2; output = w * P / (P + strength * sigma^2).
// The lowpass residual is kept. Separate luma / chroma strengths; detail recovery re-adds a
// fraction of the removed part of the finest band.
#pragma once
#include "dn_profile.h"
#include "vv/image.h"

namespace dn {

struct SpatialParams {
  float lumaStrength = 1.0f;    // multiplies sigma^2 in the Wiener gain (0 = off)
  float chromaStrength = 1.5f;
  float window = 1.0f;          // Gaussian sigma (px) of the local energy window, at band 0
  float detailRecovery = 0.0f;  // 0..1: fraction of the removed finest-band coefficient re-added
  float pilotTau = 1.0f;        // stage-1 threshold in noise std (energy E - tau^2 sigma^2)
};

/** RGBA in, RGBA out (alpha untouched). `profile` must be measured in starlet bands
 *  (measureProfileStarlet or an estimator in the same domain). */
vv::Image denoiseSpatial(const vv::Image& noisy, const NoiseProfile& profile, const SpatialParams& p = {});

}  // namespace dn
