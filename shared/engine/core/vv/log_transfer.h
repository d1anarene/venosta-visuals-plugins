#pragma once
#include <cmath>

namespace vv {

// ACEScct scalar transfer equations, independently implemented from the Academy specification:
// https://docs.acescentral.com/encodings/acescct/
// These functions apply ONLY the transfer curve. Full ACEScct colour requires AP1 primaries;
// applying this curve to another RGB gamut does not convert that gamut to ACEScct.
// Linear toe preserves negative values; floating results are deliberately not clipped to 0..1.
inline float acescctEncode(float linear) {
  constexpr double a = 10.5402377416545, b = 0.0729055341958355;
  return static_cast<float>(linear <= 0.0078125f ? a * linear + b : (std::log2(double(linear)) + 9.72) / 17.52);
}

inline float acescctDecode(float encoded) {
  constexpr double a = 10.5402377416545, b = 0.0729055341958355;
  return static_cast<float>(encoded <= 0.155251141552511f ? (double(encoded) - b) / a : std::exp2(double(encoded) * 17.52 - 9.72));
}

}  // namespace vv
