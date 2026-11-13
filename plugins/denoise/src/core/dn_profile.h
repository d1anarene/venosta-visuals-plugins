// Denoise — noise profile.
//
// Model: in each Laplacian band (dn_pyramid.h) the noise of RGB channel c has a standard deviation
// that depends on that channel's own encoded value v_c (signal-dependent noise follows each
// channel, not luma), sigma[band][c][bin(v_c)], plus a correlation between channels per band
// (colour matrices and demosaicing correlate channel noise). The covariance at a pixel is
//   S_ij = rho_ij sigma_i(v_i) sigma_j(v_j),
// and the noise of the orthonormal opponent channels the denoiser works in (dn_color.h) follows
// exactly as W S W^T. Any other transform can derive its own noise levels from this model by
// synthesising noise with it (deterministic Monte Carlo).
#pragma once
#include <cstdint>

#include "vv/image.h"
#include "vv/kernels.h"

namespace dn {

constexpr int kBands = 4;  // finest four Laplacian bands
constexpr int kBins = 16;  // encoded value bins over 0..1 (values outside use the end bins)

struct NoiseProfile {
  float sigma[kBands][3][kBins] = {};  // RGB channel noise std per band and value bin
  float corr[kBands][3] = {};          // channel correlation per band: RG, RB, GB
  /** Interpolated between bin centres; constant beyond the first/last centre. */
  float at(int band, int channel, float value) const;
  /** Noise std of the three opponent channels at a pixel with encoded RGB `rgb`. */
  vv_float3 opponentSigma(int band, vv_float3 rgb) const;
  /** Every sigma multiplied by s (a global "Noise Level" control). */
  NoiseProfile scaled(float s) const;
};

/** Opponent-space plane (3 channels) from the RGB of an image, and back (alpha from `alphaFrom`). */
vv::Plane toOpponent(const vv::Image& img);
vv::Image fromOpponent(const vv::Plane& opp, const vv::Image& alphaFrom);
/** RGB plane (3 channels) of an image. */
vv::Plane toRgbPlane(const vv::Image& img);

/** Ground truth: profile of a known noise field (noisy - clean RGB planes), each channel binned by
 *  the clean value of that channel. Bins without samples are filled from their neighbours. */
NoiseProfile measureProfile(const vv::Plane& noisyRgb, const vv::Plane& cleanRgb);

/** Ground truth in the bands of the a trous transform (dn_starlet.h), binned by the clean
 *  starlet lowpass below each band: the profile the spatial denoiser consumes. */
NoiseProfile measureProfileStarlet(const vv::Plane& noisyRgb, const vv::Plane& cleanRgb);

struct EstimateStats {
  int blocks[kBands][3][kBins] = {};
  bool binMeasured[kBands][3][kBins] = {};
};

/** Blind estimate from a noisy frame alone (see dn_profile.cpp for the method). */
NoiseProfile estimateProfile(const vv::Plane& noisyRgb, EstimateStats* stats = nullptr);

/** Temporal estimate from two frames of the same shot (consecutive, or motion-compensated):
 *  static areas give the noise directly, independent of texture; moving areas are rejected. */
NoiseProfile estimateProfileTemporal(const vv::Plane& frameA, const vv::Plane& frameB, EstimateStats* stats = nullptr);

}  // namespace dn
