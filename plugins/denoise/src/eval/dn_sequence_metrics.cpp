#include "dn_sequence_metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace dn {
namespace eval {
namespace {

size_t pixelCount(int width, int height) {
  if (width <= 0 || height <= 0 ||
      size_t(width) > std::numeric_limits<size_t>::max() / 4 / size_t(height))
    throw std::invalid_argument("sequence metric image dimensions must be positive and representable");
  return size_t(width) * size_t(height);
}

void validateImage(const vv::Image& image) {
  if (image.px.size() != pixelCount(image.width, image.height) * 4)
    throw std::invalid_argument("sequence metric image storage does not match dimensions");
  for (float v : image.px)
    if (!std::isfinite(v)) throw std::invalid_argument("sequence metric image contains nonfinite data");
}

void validatePair(const vv::Image& a, const vv::Image& b) {
  validateImage(a);
  validateImage(b);
  if (a.width != b.width || a.height != b.height)
    throw std::invalid_argument("sequence metric image dimensions differ");
}

void validatePlane(const vv::Plane& plane, int width, int height, int channels, bool mask) {
  if (plane.width != width || plane.height != height || plane.channels != channels ||
      plane.v.size() != pixelCount(width, height) * size_t(channels))
    throw std::invalid_argument("sequence metric plane dimensions/channels/storage differ");
  for (float v : plane.v) {
    if (!std::isfinite(v)) throw std::invalid_argument("sequence metric plane contains nonfinite data");
    if (mask && (v < 0.f || v > 1.f))
      throw std::invalid_argument("sequence metric mask values must be within [0,1]");
  }
}

RgbError rgbError(const vv::Image& output, const vv::Image& reference, const vv::Plane* mask) {
  RgbError result;
  double squared = 0;
  std::array<double, 3> sum = {{0, 0, 0}};
  uint64_t n = 0;
  for (size_t p = 0; p < output.px.size() / 4; ++p) {
    if (mask && mask->v[p] <= 0.5f) continue;
    ++n;
    for (int c = 0; c < 3; ++c) {
      const double e = double(output.px[p * 4 + c]) - reference.px[p * 4 + c];
      sum[c] += e;
      squared += e * e;
    }
  }
  if (n) {
    result.rmse = {std::sqrt(squared / (3.0 * double(n))), n};
    for (int c = 0; c < 3; ++c) result.bias[c] = sum[c] / double(n);
  }
  return result;
}

double laplacian(const vv::Image& image, int x, int y, int c) {
  return 4.0 * image.at(x, y)[c] - image.at(x - 1, y)[c] - image.at(x + 1, y)[c] -
         image.at(x, y - 1)[c] - image.at(x, y + 1)[c];
}

double residualAt(const vv::Image& output, const vv::Image& reference, int x, int y, int c) {
  return double(output.at(x, y)[c]) - reference.at(x, y)[c];
}

}  // namespace

RgbError maskedRgbError(const vv::Image& output, const vv::Image& reference, const vv::Plane& mask) {
  validatePair(output, reference);
  validatePlane(mask, output.width, output.height, 1, true);
  return rgbError(output, reference, &mask);
}

FrameQuality frameQuality(const vv::Image& output, const vv::Image& reference,
                          const vv::Plane& detailMask, const vv::Plane& flatMask,
                          const vv::Plane& disocclusionMask) {
  validatePair(output, reference);
  const int w = output.width, h = output.height;
  validatePlane(detailMask, w, h, 1, true);
  validatePlane(flatMask, w, h, 1, true);
  validatePlane(disocclusionMask, w, h, 1, true);
  FrameQuality result;
  result.all = rgbError(output, reference, nullptr);
  result.disocclusionRmse = rgbError(output, reference, &disocclusionMask).rmse;

  double cross = 0, energy = 0;
  uint64_t detailN = 0;
  for (int y = 1; y + 1 < h; ++y)
    for (int x = 1; x + 1 < w; ++x) {
      if (detailMask.at(x, y)[0] <= 0.5f || detailMask.at(x - 1, y)[0] <= 0.5f ||
          detailMask.at(x + 1, y)[0] <= 0.5f || detailMask.at(x, y - 1)[0] <= 0.5f ||
          detailMask.at(x, y + 1)[0] <= 0.5f)
        continue;
      ++detailN;
      for (int c = 0; c < 3; ++c) {
        const double cleanDetail = laplacian(reference, x, y, c);
        const double outDetail = laplacian(output, x, y, c);
        energy += cleanDetail * cleanDetail;
        cross += cleanDetail * outDetail;
      }
    }
  if (detailN && energy > 1e-24) {
    const double transfer = cross / energy;
    result.detailTransfer = {transfer, detailN};
    result.removedStructure = {1.0 - transfer, detailN};
  }

  // Welford variance avoids subtracting two large sums in a biased/extended-range region.
  std::array<double, 3> mean = {{0, 0, 0}}, m2 = {{0, 0, 0}};
  uint64_t flatN = 0;
  for (size_t p = 0; p < output.px.size() / 4; ++p) {
    if (flatMask.v[p] <= 0.5f) continue;
    ++flatN;
    for (int c = 0; c < 3; ++c) {
      const double e = double(output.px[p * 4 + c]) - reference.px[p * 4 + c];
      const double delta = e - mean[c];
      mean[c] += delta / double(flatN);
      m2[c] += delta * (e - mean[c]);
    }
  }
  // A single observation has no empirical noise variance estimate.
  if (flatN >= 2)
    result.flatResidualSigma = {std::sqrt(std::max(0.0, (m2[0] + m2[1] + m2[2]) /
                                                         (3.0 * double(flatN - 1)))), flatN};
  return result;
}

Metric temporalResidualError(const vv::Image& output, const vv::Image& reference,
                             const vv::Image& previousOutput, const vv::Image& previousReference,
                             const vv::Plane& backwardFlow, const vv::Plane& visibleMask) {
  validatePair(output, reference);
  validatePair(previousOutput, previousReference);
  if (output.width != previousOutput.width || output.height != previousOutput.height)
    throw std::invalid_argument("temporal metric current/previous dimensions differ");
  const int w = output.width, h = output.height;
  validatePlane(backwardFlow, w, h, 2, false);
  validatePlane(visibleMask, w, h, 1, true);
  double squared = 0;
  uint64_t n = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (visibleMask.at(x, y)[0] <= 0.5f) continue;
      const double px = double(x) + backwardFlow.at(x, y)[0];
      const double py = double(y) + backwardFlow.at(x, y)[1];
      if (px < 0 || px > w - 1 || py < 0 || py > h - 1) continue;
      const int x0 = int(std::floor(px)), y0 = int(std::floor(py));
      const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
      const double fx = px - x0, fy = py - y0;
      ++n;
      for (int c = 0; c < 3; ++c) {
        const double a = residualAt(previousOutput, previousReference, x0, y0, c);
        const double b = residualAt(previousOutput, previousReference, x1, y0, c);
        const double d = residualAt(previousOutput, previousReference, x0, y1, c);
        const double e = residualAt(previousOutput, previousReference, x1, y1, c);
        const double previous = (1.0 - fy) * ((1.0 - fx) * a + fx * b) + fy * ((1.0 - fx) * d + fx * e);
        const double difference = residualAt(output, reference, x, y, c) - previous;
        squared += difference * difference;
      }
    }
  return n ? Metric{std::sqrt(squared / (3.0 * double(n))), n} : Metric{};
}

}  // namespace eval
}  // namespace dn
