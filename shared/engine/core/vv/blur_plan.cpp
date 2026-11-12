#include "blur_plan.h"

#include <algorithm>
#include <cmath>

namespace vv {

float pyramidVariance(int level) {
  return 1.25f * (std::pow(4.0f, static_cast<float>(level)) - 1.0f) / 3.0f;
}

BlurStep planGaussian(float sigma, int w, int h) {
  BlurStep s;
  int L = 0;
  while (sigma / std::pow(2.0f, static_cast<float>(L + 1)) >= 2.0f && (h >> (L + 1)) >= 8 && (w >> (L + 1)) >= 8) ++L;
  s.level = L;
  s.sigmaAtLevel = std::sqrt(std::max(sigma * sigma - pyramidVariance(L), 0.25f)) / std::pow(2.0f, static_cast<float>(L));
  return s;
}

}  // namespace vv
