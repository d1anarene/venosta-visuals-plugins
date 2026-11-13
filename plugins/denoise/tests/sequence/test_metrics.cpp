// Standalone tests for sequence metrics. No denoising algorithm is exercised here.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "dn_sequence_metrics.h"
#include "vv/blur.h"

using namespace dn::eval;

namespace {
int passed = 0, failed = 0;
#define CHECK(condition) do { if (condition) ++passed; else { \
  ++failed; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

bool close(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) <= tolerance; }

template <typename F> bool invalid(F f) {
  try { f(); } catch (const std::invalid_argument&) { return true; }
  return false;
}

vv::Plane mask(int w, int h, float value) {
  vv::Plane p(w, h, 1);
  std::fill(p.v.begin(), p.v.end(), value);
  return p;
}

vv::Image image(int w, int h, float constant = 0.4f) {
  vv::Image im(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < 3; ++c) im.at(x, y)[c] = constant;
      im.at(x, y)[3] = 1.f;
    }
  return im;
}

vv::Image texture(int w, int h, float translation = 0.f) {
  vv::Image im = image(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c)
        im.at(x, y)[c] = float(0.45 + 0.15 * std::sin((x - translation) * 0.8 + c * 0.2) +
                              0.06 * std::cos(y * 0.9 + c));
  return im;
}

void clean_and_bias() {
  const int w = 41, h = 33;
  const vv::Image ref = texture(w, h);
  const vv::Plane all = mask(w, h, 1.f);
  const FrameQuality q = frameQuality(ref, ref, all, all, all);
  CHECK(q.all.rmse.samples == uint64_t(w * h));
  CHECK(q.all.rmse.value == 0.0);
  CHECK(q.all.bias[0] == 0.0 && q.all.bias[1] == 0.0 && q.all.bias[2] == 0.0);
  CHECK(close(q.detailTransfer.value, 1.0, 1e-12));
  CHECK(q.detailTransfer.samples == uint64_t((w - 2) * (h - 2)));
  CHECK(q.removedStructure.value == 0.0);
  CHECK(q.flatResidualSigma.value == 0.0);
  CHECK(q.disocclusionRmse.value == 0.0);
  vv::Plane flow(w, h, 2);
  CHECK(temporalResidualError(ref, ref, ref, ref, flow, all).value == 0.0);

  vv::Image shifted = ref;
  const double bias[3] = {0.1, -0.05, 0.02};
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < 3; ++c) shifted.at(x, y)[c] += float(bias[c]);
      shifted.at(x, y)[3] = 0.25f;  // RGB metrics deliberately exclude alpha.
    }
  const FrameQuality b = frameQuality(shifted, ref, all, all, all);
  CHECK(close(b.all.rmse.value, std::sqrt((0.01 + 0.0025 + 0.0004) / 3.0)));
  for (int c = 0; c < 3; ++c) CHECK(close(b.all.bias[c], bias[c]));
  CHECK(close(b.detailTransfer.value, 1.0));
  CHECK(b.flatResidualSigma.value < 1e-7);
  CHECK(b.all.rmse.value > 0.06);  // Zero variance does not hide systematic colour error.
}

void masks_and_degenerate_cases() {
  const vv::Image ref = image(5, 4);
  const vv::Plane empty = mask(5, 4, 0.f), all = mask(5, 4, 1.f);
  const RgbError e = maskedRgbError(ref, ref, empty);
  CHECK(e.rmse.samples == 0 && std::isnan(e.rmse.value) && std::isnan(e.bias[0]));
  const FrameQuality q = frameQuality(ref, ref, empty, empty, empty);
  CHECK(q.detailTransfer.samples == 0 && std::isnan(q.detailTransfer.value));
  CHECK(q.removedStructure.samples == 0 && std::isnan(q.removedStructure.value));
  CHECK(q.flatResidualSigma.samples == 0 && std::isnan(q.flatResidualSigma.value));
  CHECK(q.disocclusionRmse.samples == 0 && std::isnan(q.disocclusionRmse.value));
  CHECK(std::isnan(frameQuality(ref, ref, all, all, all).detailTransfer.value));
  vv::Plane flow(5, 4, 2);
  CHECK(std::isnan(temporalResidualError(ref, ref, ref, ref, flow, empty).value));

  const vv::Image one = image(1, 1, -0.2f);  // Extended-range pixels are not silently clipped.
  const vv::Plane m = mask(1, 1, 1.f);
  const FrameQuality tiny = frameQuality(one, one, m, m, m);
  CHECK(tiny.all.rmse.samples == 1 && tiny.all.rmse.value == 0);
  CHECK(std::isnan(tiny.detailTransfer.value) && std::isnan(tiny.flatResidualSigma.value));
  vv::Plane zero(1, 1, 2);
  CHECK(temporalResidualError(one, one, one, one, zero, m).samples == 1);

  vv::Image out = image(2, 1, 0.f), clean = out;
  for (int c = 0; c < 3; ++c) { out.at(0, 0)[c] = -0.1f; out.at(1, 0)[c] = 0.1f; }
  const vv::Plane two = mask(2, 1, 1.f);
  CHECK(close(frameQuality(out, clean, two, two, two).flatResidualSigma.value, std::sqrt(0.02)));
}

void detail_loss_and_noise() {
  const int w = 65, h = 65;
  const vv::Image ref = texture(w, h);
  vv::Plane p(w, h, 4);
  p.v = ref.px;
  vv::Image blurred = ref;
  blurred.px = vv::gaussianBlurDirect(p, 1.1f).v;
  const vv::Plane all = mask(w, h, 1.f);
  const FrameQuality q = frameQuality(blurred, ref, all, all, all);
  CHECK(q.detailTransfer.value < 0.8);
  CHECK(q.removedStructure.value > 0.2);
  CHECK(q.all.rmse.value > 0.02);

  // Detail present only in x; unrelated high-frequency y pattern must not count as preservation.
  vv::Image xDetail = image(w, h), unrelated = xDetail;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) {
        xDetail.at(x, y)[c] += float(0.1 * std::sin(x * 6.283185307179586 / 8.0));
        unrelated.at(x, y)[c] += float(0.3 * std::sin(y * 6.283185307179586 / 8.0));
      }
  const FrameQuality n = frameQuality(unrelated, xDetail, all, all, all);
  CHECK(std::fabs(n.detailTransfer.value) < 1e-5);
  CHECK(n.removedStructure.value > 0.999);
  CHECK(n.all.rmse.value > 0.1);
}

void moving_frozen_and_trailing_outputs() {
  const int w = 48, h = 32;
  const vv::Image previous = texture(w, h), current = texture(w, h, 2.f);
  const vv::Plane all = mask(w, h, 1.f);
  vv::Plane flow(w, h, 2);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) flow.at(x, y)[0] = -2.f;
  const Metric correct = temporalResidualError(current, current, previous, previous, flow, all);
  const Metric frozen = temporalResidualError(previous, current, previous, previous, flow, all);
  CHECK(correct.value == 0.0);
  CHECK(correct.samples == uint64_t((w - 2) * h));
  CHECK(frozen.value > 0.1);
  CHECK(frameQuality(previous, current, all, all, all).all.rmse.value > 0.1);

  vv::Image before = image(w, h, 0.2f), after = before;
  for (int y = 8; y < 16; ++y)
    for (int x = 5; x < 13; ++x)
      for (int c = 0; c < 3; ++c) before.at(x, y)[c] = 0.8f;
  for (int y = 8; y < 16; ++y)
    for (int x = 15; x < 23; ++x)
      for (int c = 0; c < 3; ++c) after.at(x, y)[c] = 0.8f;
  vv::Plane disoccluded = mask(w, h, 0.f);
  for (int y = 8; y < 16; ++y)
    for (int x = 5; x < 13; ++x) disoccluded.at(x, y)[0] = 1.f;
  vv::Image trailing = after;
  for (size_t i = 0; i < trailing.px.size(); ++i) trailing.px[i] = (after.px[i] + before.px[i]) * 0.5f;
  const FrameQuality q = frameQuality(trailing, after, all, all, disoccluded);
  CHECK(q.disocclusionRmse.samples == 64);
  CHECK(close(q.disocclusionRmse.value, 0.3));
  CHECK(frameQuality(after, after, all, all, disoccluded).disocclusionRmse.value == 0.0);
}

void subpixel_residual_motion_and_bounds() {
  const int w = 8, h = 6;
  vv::Image clean = texture(w, h), previousClean = texture(w, h, 0.5f);
  vv::Image out = clean, previousOut = previousClean;
  vv::Plane flow(w, h, 2);
  const vv::Plane all = mask(w, h, 1.f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      flow.at(x, y)[0] = -0.5f;
      flow.at(x, y)[1] = 0.25f;
      for (int c = 0; c < 3; ++c) {
        previousOut.at(x, y)[c] += float(x * 0.01 + y * 0.02);
        out.at(x, y)[c] += float((x - 0.5) * 0.01 + (y + 0.25) * 0.02);
      }
    }
  const Metric q = temporalResidualError(out, clean, previousOut, previousClean, flow, all);
  CHECK(q.samples == 35);  // x=1..7, y=0..4; no clamped out-of-bounds warps.
  CHECK(q.value < 1e-7);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) flow.at(x, y)[0] = 1000.f;
  const Metric none = temporalResidualError(out, clean, previousOut, previousClean, flow, all);
  CHECK(none.samples == 0 && std::isnan(none.value));
}

void malformed_inputs() {
  const vv::Image im = image(7, 5);
  const vv::Plane all = mask(7, 5, 1.f);
  const vv::Plane flow(7, 5, 2);
  vv::Image shortImage = im;
  shortImage.px.pop_back();
  CHECK(invalid([&] { maskedRgbError(shortImage, im, all); }));
  CHECK(invalid([&] { maskedRgbError(im, image(5, 7), all); }));
  vv::Image nonfinite = im;
  nonfinite.at(1, 1)[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK(invalid([&] { frameQuality(nonfinite, im, all, all, all); }));
  vv::Plane bad = all;
  bad.v[0] = -1.f;
  CHECK(invalid([&] { frameQuality(im, im, bad, all, all); }));
  bad = all; bad.v[0] = std::numeric_limits<float>::infinity();
  CHECK(invalid([&] { maskedRgbError(im, im, bad); }));
  CHECK(invalid([&] { temporalResidualError(im, im, im, im, all, all); }));
  vv::Plane badFlow = flow;
  badFlow.v[1] = std::numeric_limits<float>::infinity();
  CHECK(invalid([&] { temporalResidualError(im, im, im, im, badFlow, all); }));
  CHECK(invalid([&] { temporalResidualError(im, im, image(6, 5), image(6, 5), flow, all); }));
  CHECK(invalid([&] { maskedRgbError(vv::Image{}, vv::Image{}, vv::Plane{}); }));
}
}  // namespace

int main() {
  clean_and_bias();
  masks_and_degenerate_cases();
  detail_loss_and_noise();
  moving_frozen_and_trailing_outputs();
  subpixel_residual_motion_and_bounds();
  malformed_inputs();
  std::printf("sequence metrics: %d checks passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
