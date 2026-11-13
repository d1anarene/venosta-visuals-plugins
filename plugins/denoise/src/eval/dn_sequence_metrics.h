// Denoise research metrics. All values are in the caller's explicit RGB encoding/domain;
// no transfer, gamut, clipping, alpha weighting or display conversion is performed here.
#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "vv/image.h"

namespace dn {
namespace eval {

/** samples counts contributing pixel centres, not RGB components. Undefined metrics have
 *  value=NaN and samples=0, including detail regression with zero clean detail energy. */
struct Metric {
  double value = std::numeric_limits<double>::quiet_NaN();
  uint64_t samples = 0;
};

struct RgbError {
  Metric rmse;
  // Mean output-reference per RGB channel; NaN when no samples exist.
  std::array<double, 3> bias = {{std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::quiet_NaN()}};
};

/** Binary inclusion is mask>0.5. Mask must be one channel and finite in [0,1]. */
RgbError maskedRgbError(const vv::Image& output, const vv::Image& reference, const vv::Plane& mask);

struct FrameQuality {
  RgbError all;
  // Through-origin regression of output 4-neighbour Laplacians on clean Laplacians.
  // 1=clean contrast retained; <1=attenuation; can exceed 1 with sharpening/noise correlation.
  Metric detailTransfer;
  // Regression of (clean-output) detail on clean detail, equal to 1-detailTransfer.
  // This is structure leakage, not residual-noise amount, and must not be clamped to [0,1].
  Metric removedStructure;
  // Pooled RGB standard deviation of output-reference in the supplied flat region,
  // after subtracting each channel's residual mean. all.bias separately exposes colour shifts.
  Metric flatResidualSigma;
  Metric disocclusionRmse;
};

/** Detail centres need all four adjacent pixels inside detailMask and image bounds. This avoids
 *  measuring a region boundary as texture. RGB is evaluated independently with equal weight.
 *  No RGB luma coefficient is assumed. All metrics exclude alpha but require finite RGBA data.
 *  Dimension/storage/finite-value/mask violations throw std::invalid_argument. */
FrameQuality frameQuality(const vv::Image& output, const vv::Image& reference,
                          const vv::Plane& detailMask, const vv::Plane& flatMask,
                          const vv::Plane& disocclusionMask);

/** RMSE of current output-clean residual minus bilinearly warped previous output-clean residual.
 *  Flow stores pixel displacement current->previous (not absolute positions); only visibleMask>0.5
 *  and fully in-bounds interpolation footprints contribute. At exact integer coordinates zero
 *  interpolation weights do not require another pixel. Visibility is ground-truth visibility,
 *  independent of the restoration method. Pair with frame/detail/disocclusion errors: temporal
 *  stability alone rewards frozen/blurred output. The same validation/undefined rules apply. */
Metric temporalResidualError(const vv::Image& output, const vv::Image& reference,
                             const vv::Image& previousOutput, const vv::Image& previousReference,
                             const vv::Plane& backwardFlow, const vv::Plane& visibleMask);

}  // namespace eval
}  // namespace dn
