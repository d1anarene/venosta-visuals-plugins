// Blind noise estimation.
//
// Per band and RGB channel: the band is cut into half-overlapping 8x8 blocks; for each block the
// mean squared coefficient ("variance": band-pass coefficients have zero mean, and removing each
// block's mean would remove a spectrum-dependent part of correlated noise) and the block's mean channel value (from the low-pass image) are recorded.
// Texture only ever adds variance, so the least-textured blocks are mostly noise: the estimate
// for a value bin is a low quantile (10 %) of the block variances in that bin, times a correction
// factor (mean / 10 % quantile of block variances of pure noise through the same band filter).
//
// Refinement (iterated): blocks whose variance, divided by the current curve at the block's own
// value, is below the 99 % point of pure noise count as noise-only; each bin is re-estimated from
// their truncation-corrected mean, normalised to the bin centre (noise can change a lot inside one
// bin near black in gamma-encoded footage). Uses most blocks instead of the lowest tenth.
//
// Trust: a bin is used only if the spread of its block variances (50 % / 10 % quantile) is close
// to that of pure noise; bins made of edges or texture are interpolated from trusted neighbours.
// Limitation (measured, docs/RESEARCH.md): uniform fine texture everywhere (no flat area at all)
// is indistinguishable from noise for this spatial method.
//
// Channel correlation: from the blocks whose normalised variance is low in all three channels.
#include "dn_profile.h"

#include "vv/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "dn_color.h"
#include "dn_pyramid.h"
#include "dn_starlet.h"

namespace dn {
namespace {

int binOf(float v) { return std::clamp(int((std::isfinite(v) ? v : 0.f) * kBins), 0, kBins - 1); }

// Fill unmeasured bins by linear interpolation between measured neighbours, constant beyond the
// ends; with nothing measured, use `fallback`.
void fillRow(float* row, const bool* ok, float fallback) {
  int first = -1, last = -1;
  for (int i = 0; i < kBins; ++i)
    if (ok[i]) { if (first < 0) first = i; last = i; }
  if (first < 0) {
    for (int i = 0; i < kBins; ++i) row[i] = fallback;
    return;
  }
  for (int i = 0; i < first; ++i) row[i] = row[first];
  for (int i = last + 1; i < kBins; ++i) row[i] = row[last];
  int prev = first;
  for (int i = first + 1; i <= last; ++i) {
    if (!ok[i]) continue;
    for (int j = prev + 1; j < i; ++j) row[j] = row[prev] + (row[i] - row[prev]) * float(j - prev) / float(i - prev);
    prev = i;
  }
}

constexpr int kBlock = 8, kStride = 4, kMinBlocks = 6;
constexpr float kQuantile = 0.1f;
constexpr float kSpreadTolerance = 2.0f;
constexpr float kMaxSlope = 1.5f;  // max |d ln sigma / d ln v| between bins

float quantile(std::vector<float>& v, float q) {
  std::sort(v.begin(), v.end());
  return v[size_t(q * float(v.size() - 1))];
}

struct Block {
  float var[3], value[3], cov[3];  // cov: RG, RB, GB
};

std::vector<Block> blocks(const vv::Plane& band, const vv::Plane& low) {
  const int rows = band.height >= kBlock ? (band.height - kBlock) / kStride + 1 : 0;
  const int cols = band.width >= kBlock ? (band.width - kBlock) / kStride + 1 : 0;
  std::vector<Block> out(size_t(rows) * size_t(cols));
  vv::parallelRows(rows, [&](int r) {
    const int by = r * kStride;
    for (int q = 0; q < cols; ++q) {
      const int bx = q * kStride;
      double s2[3] = {}, sv[3] = {}, sx[3] = {};
      for (int y = by; y < by + kBlock; ++y)
        for (int x = bx; x < bx + kBlock; ++x) {
          const float* p = band.at(x, y);
          const float* l = low.at(x, y);
          for (int c = 0; c < 3; ++c) { s2[c] += double(p[c]) * p[c]; sv[c] += l[c]; }
          sx[0] += double(p[0]) * p[1]; sx[1] += double(p[0]) * p[2]; sx[2] += double(p[1]) * p[2];
        }
      const double n = kBlock * kBlock;
      Block& b = out[size_t(r) * size_t(cols) + size_t(q)];
      for (int c = 0; c < 3; ++c) {
        b.var[c] = float(s2[c] / n);  // band coefficients have zero mean: no mean removal (see top)
        b.value[c] = float(sv[c] / n);
      }
      b.cov[0] = float(sx[0] / n);
      b.cov[1] = float(sx[1] / n);
      b.cov[2] = float(sx[2] / n);
    }
  }, 4);
  return out;
}

// Statistics of block variances of pure white Gaussian noise through each band filter (512x512,
// fixed seed), all relative to the true variance: the 10 % quantile (start value), the 95 %
// quantile (rejection threshold; 99 %) and the mean of the blocks below that threshold (truncation
// correction).
struct Calib {
  float q10, q95, truncMean, spread;  // spread: 50 % / 10 % quantile
};
// Bands of `signal` in a domain: Laplacian pyramid, a trous (starlet) or the pixels themselves.
std::vector<vv::Plane> bandsOf(Domain d, const vv::Plane& signal) {
  std::vector<vv::Plane> out;
  if (d == Domain::Pixel) { out.push_back(signal); return out; }
  if (d == Domain::Starlet) return starletDecompose(signal, kBands).bands;
  Pyramid p = decompose(signal, kBands);
  for (int b = 0; b + 1 < int(p.bands.size()) && b < kBands; ++b) out.push_back(std::move(p.bands[size_t(b)]));
  return out;
}
// The lowpass used to bin band b by value, in the same domain.
std::vector<vv::Plane> lowsOf(Domain d, const vv::Plane& values, int count) {
  std::vector<vv::Plane> out;
  if (d == Domain::Pixel) { out.push_back(values); return out; }
  if (d == Domain::Starlet) {
    Starlet s = starletDecompose(values, kBands);
    for (int b = 0; b < count; ++b) out.push_back(std::move(s.smooth[size_t(b)]));
    return out;
  }
  for (int b = 0; b < count; ++b) out.push_back(bandLowpass(values, b));
  return out;
}

std::array<Calib, kBands> computeCalibration(Domain d) {
  std::array<Calib, kBands> cal{};
  vv::Plane noise(512, 512, 3);
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x)
      for (int c = 0; c < 3; ++c) {
        const vv_uint h1 = vv_hash3(vv_uint(x), vv_uint(y), 0x51u + vv_uint(c)), h2 = vv_hash(h1 + 0x9E3779B9u);
        const float u1 = (float(h1 >> 8) + 0.5f) / 16777216.f, u2 = float(h2 >> 8) / 16777216.f;
        noise.at(x, y)[c] = std::sqrt(-2.f * std::log(u1)) * std::cos(6.2831853f * u2);
      }
  const std::vector<vv::Plane> bands = bandsOf(d, noise);
  for (int b = 0; b < int(bands.size()); ++b) {
    const vv::Plane& band = bands[size_t(b)];
    std::vector<float> v;
    double total = 0;
    for (const Block& bl : blocks(band, band))
      for (int c = 0; c < 3; ++c) v.push_back(bl.var[c]);
    for (float q : band.v) total += double(q) * q;
    const float mean = float(total / double(band.v.size()));
    Calib& c = cal[size_t(b)];
    c.q10 = quantile(v, kQuantile) / mean;
    c.q95 = quantile(v, 0.99f) / mean;
    c.spread = quantile(v, 0.5f) / quantile(v, kQuantile);
    double s = 0;
    size_t n = 0;
    for (float q : v)
      if (q / mean < c.q95) { s += q / mean; ++n; }
    c.truncMean = float(s / double(n));
  }
  return cal;
}
const Calib* calibration(Domain d) {
  static const std::array<Calib, kBands> lap = computeCalibration(Domain::Laplacian);  // thread-safe init
  static const std::array<Calib, kBands> sta = computeCalibration(Domain::Starlet);
  static const std::array<Calib, kBands> pix = computeCalibration(Domain::Pixel);
  return d == Domain::Starlet ? sta.data() : d == Domain::Pixel ? pix.data() : lap.data();
}

float centre(int k) { return (float(k) + 0.5f) / kBins; }

}  // namespace

float NoiseProfile::at(int band, int channel, float value) const {
  const float* row = sigma[band][channel];
  const float t = (std::isfinite(value) ? value : 0.f) * kBins - 0.5f;
  if (t <= 0.f) return row[0];
  if (t >= kBins - 1) return row[kBins - 1];
  const int i = int(t);
  return row[i] + (row[i + 1] - row[i]) * (t - float(i));
}

vv_float3 NoiseProfile::opponentSigma(int band, vv_float3 rgb) const {
  const float s[3] = {at(band, 0, rgb.x), at(band, 1, rgb.y), at(band, 2, rgb.z)};
  const float* r = corr[band];
  const float S[3][3] = {{s[0] * s[0], r[0] * s[0] * s[1], r[1] * s[0] * s[2]},
                         {r[0] * s[0] * s[1], s[1] * s[1], r[2] * s[1] * s[2]},
                         {r[1] * s[0] * s[2], r[2] * s[1] * s[2], s[2] * s[2]}};
  static const float W[3][3] = {{DN_INV_SQRT3, DN_INV_SQRT3, DN_INV_SQRT3},
                                {DN_INV_SQRT2, 0.f, -DN_INV_SQRT2},
                                {DN_INV_SQRT6, -2.f * DN_INV_SQRT6, DN_INV_SQRT6}};
  float o[3];
  for (int k = 0; k < 3; ++k) {
    double v = 0;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) v += double(W[k][i]) * W[k][j] * S[i][j];
    o[k] = float(std::sqrt(std::max(v, 0.0)));
  }
  return vv_make3(o[0], o[1], o[2]);
}

NoiseProfile NoiseProfile::scaled(float s) const {
  NoiseProfile p = *this;
  for (auto& b : p.sigma)
    for (auto& c : b)
      for (float& v : c) v *= s;
  return p;
}

vv::Plane toRgbPlane(const vv::Image& img) {
  vv::Plane o(img.width, img.height, 3);
  for (size_t i = 0, n = size_t(img.width) * img.height; i < n; ++i)
    for (int c = 0; c < 3; ++c) o.v[i * 3 + c] = img.px[i * 4 + c];
  return o;
}

vv::Plane toOpponent(const vv::Image& img) {
  vv::Plane o(img.width, img.height, 3);
  for (size_t i = 0, n = size_t(img.width) * img.height; i < n; ++i) {
    const float* s = &img.px[i * 4];
    const vv_float3 c = dn_to_opponent(vv_make3(s[0], s[1], s[2]));
    o.v[i * 3] = c.x; o.v[i * 3 + 1] = c.y; o.v[i * 3 + 2] = c.z;
  }
  return o;
}

vv::Image fromOpponent(const vv::Plane& opp, const vv::Image& alphaFrom) {
  vv::Image img(opp.width, opp.height);
  for (size_t i = 0, n = size_t(opp.width) * opp.height; i < n; ++i) {
    const float* s = &opp.v[i * 3];
    const vv_float3 c = dn_from_opponent(vv_make3(s[0], s[1], s[2]));
    img.px[i * 4] = c.x; img.px[i * 4 + 1] = c.y; img.px[i * 4 + 2] = c.z; img.px[i * 4 + 3] = alphaFrom.px[i * 4 + 3];
  }
  return img;
}

namespace {
// Shared by both ground-truth measurements: noise bands, and the lowpass used to bin each band.
NoiseProfile measureFromBands(const std::vector<const vv::Plane*>& bands, const std::vector<const vv::Plane*>& lows) {
  NoiseProfile prof;
  for (int b = 0; b < kBands && b < int(bands.size()); ++b) {
    const vv::Plane& band = *bands[size_t(b)];
    const vv::Plane& low = *lows[size_t(b)];
    double s2[3][kBins] = {}, sx[3] = {}, sq[3] = {};
    long n[3][kBins] = {};
    for (int y = 0; y < band.height; ++y)
      for (int x = 0; x < band.width; ++x) {
        const float* q = band.at(x, y);
        for (int c = 0; c < 3; ++c) {
          const int k = binOf(low.at(x, y)[c]);
          s2[c][k] += double(q[c]) * q[c]; ++n[c][k]; sq[c] += double(q[c]) * q[c];
        }
        sx[0] += double(q[0]) * q[1]; sx[1] += double(q[0]) * q[2]; sx[2] += double(q[1]) * q[2];
      }
    for (int c = 0; c < 3; ++c) {
      bool ok[kBins];
      for (int k = 0; k < kBins; ++k) {
        ok[k] = n[c][k] >= 64;
        prof.sigma[b][c][k] = ok[k] ? float(std::sqrt(s2[c][k] / double(n[c][k]))) : 0.f;
      }
      fillRow(prof.sigma[b][c], ok, 0.f);
    }
    const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (int k = 0; k < 3; ++k) {
      const double d = std::sqrt(sq[pairs[k][0]] * sq[pairs[k][1]]);
      prof.corr[b][k] = d > 0 ? float(sx[k] / d) : 0.f;
    }
  }
  return prof;
}
}  // namespace

NoiseProfile measureProfileStarlet(const vv::Plane& noisy, const vv::Plane& clean) {
  vv::Plane diff = noisy;
  for (size_t i = 0; i < diff.v.size(); ++i) diff.v[i] -= clean.v[i];
  const Starlet d = starletDecompose(diff, kBands), c = starletDecompose(clean, kBands);
  std::vector<const vv::Plane*> bands, lows;
  for (int b = 0; b < kBands; ++b) { bands.push_back(&d.bands[size_t(b)]); lows.push_back(&c.smooth[size_t(b)]); }
  return measureFromBands(bands, lows);
}

NoiseProfile measureProfile(const vv::Plane& noisy, const vv::Plane& clean) {
  vv::Plane diff = noisy;
  for (size_t i = 0; i < diff.v.size(); ++i) diff.v[i] -= clean.v[i];
  const Pyramid p = decompose(diff, kBands);
  NoiseProfile prof;
  for (int b = 0; b < kBands && b + 1 < int(p.bands.size()); ++b) {
    const vv::Plane& band = p.bands[size_t(b)];
    const vv::Plane low = bandLowpass(clean, b);
    double s2[3][kBins] = {}, sx[3] = {}, sq[3] = {};
    long n[3][kBins] = {};
    for (int y = 0; y < band.height; ++y)
      for (int x = 0; x < band.width; ++x) {
        const float* q = band.at(x, y);
        for (int c = 0; c < 3; ++c) {
          const int k = binOf(low.at(x, y)[c]);
          s2[c][k] += double(q[c]) * q[c]; ++n[c][k]; sq[c] += double(q[c]) * q[c];
        }
        sx[0] += double(q[0]) * q[1]; sx[1] += double(q[0]) * q[2]; sx[2] += double(q[1]) * q[2];
      }
    for (int c = 0; c < 3; ++c) {
      bool ok[kBins];
      for (int k = 0; k < kBins; ++k) {
        ok[k] = n[c][k] >= 64;
        prof.sigma[b][c][k] = ok[k] ? float(std::sqrt(s2[c][k] / double(n[c][k]))) : 0.f;
      }
      fillRow(prof.sigma[b][c], ok, 0.f);
    }
    const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (int k = 0; k < 3; ++k) {
      const double d = std::sqrt(sq[pairs[k][0]] * sq[pairs[k][1]]);
      prof.corr[b][k] = d > 0 ? float(sx[k] / d) : 0.f;
    }
  }
  return prof;
}

namespace {
// Shared by the spatial and temporal estimators: band coefficients come from `signal`, the value
// used for binning from `values`.
NoiseProfile estimateImpl(const vv::Plane& signal, const vv::Plane& values, EstimateStats* stats, Domain domain) {
  const Calib* cal = calibration(domain);
  std::vector<vv::Plane> bands, lows;
  if (domain == Domain::Starlet && &signal == &values) {  // single frame: one decomposition serves both
    Starlet s = starletDecompose(signal, kBands);
    bands = std::move(s.bands);
    lows = std::move(s.smooth);
  } else {
    bands = bandsOf(domain, signal);
    lows = lowsOf(domain, values, int(bands.size()));
  }
  NoiseProfile prof;
  for (int b = 0; b < kBands && b < int(bands.size()); ++b) {
    const std::vector<Block> bl = blocks(bands[size_t(b)], lows[size_t(b)]);
    for (int c = 0; c < 3; ++c) {
      // Start: low quantile per bin (texture only adds variance).
      std::vector<float> perBin[kBins], all;
      for (const Block& k : bl) perBin[binOf(k.value[c])].push_back(k.var[c]);
      bool ok[kBins];
      for (int k = 0; k < kBins; ++k) {
        ok[k] = int(perBin[k].size()) >= kMinBlocks;
        all.insert(all.end(), perBin[k].begin(), perBin[k].end());
        if (ok[k]) {
          prof.sigma[b][c][k] = std::sqrt(quantile(perBin[k], kQuantile) / cal[b].q10);
          // Trust test: pure noise has a known spread of block variances (50 % / 10 % quantile);
          // a bin made of edges and texture is much wider. Such bins are interpolated instead.
          const float q10 = quantile(perBin[k], kQuantile), q50 = quantile(perBin[k], 0.5f);
          if (q10 <= 0.f || q50 / q10 > kSpreadTolerance * cal[b].spread) ok[k] = false;
        }
        if (stats) stats->blocks[b][c][k] = int(perBin[k].size());
      }
      const float fallback = all.empty() ? 0.f : std::sqrt(quantile(all, kQuantile) / cal[b].q10);
      fillRow(prof.sigma[b][c], ok, fallback);
      // Iterate: keep the blocks that look like pure noise under the current curve (normalised
      // variance below the 95 % point of pure noise), re-estimate each bin from their truncated
      // mean, normalised to the bin centre so noise changing inside a bin does not bias it.
      for (int it = 0; it < 6; ++it) {
        double sum[kBins] = {};
        int n[kBins] = {};
        for (const Block& k : bl) {
          const float s = prof.at(b, c, k.value[c]);
          if (s <= 0.f) continue;
          const float r = k.var[c] / (s * s);
          if (r >= cal[b].q95) continue;
          const int bin = binOf(k.value[c]);
          sum[bin] += r; ++n[bin];
        }
        float row[kBins];
        bool good[kBins];
        for (int k = 0; k < kBins; ++k) {
          good[k] = ok[k] && n[k] >= kMinBlocks;
          row[k] = good[k] ? prof.at(b, c, centre(k)) * std::sqrt(float(sum[k] / n[k]) / cal[b].truncMean) : 0.f;
        }
        fillRow(row, good, fallback);
        for (int k = 0; k < kBins; ++k) prof.sigma[b][c][k] = row[k];
      }
      // Upper envelope from the well-populated trusted bins: texture only ever raises an
      // estimate, and noise changes smoothly with the signal, at most like v^+-1.5 between bin
      // centres (the steepest case is gamma-encoded read noise near black).
      int maxCount = 0;
      for (int k = 0; k < kBins; ++k)
        if (ok[k]) maxCount = std::max(maxCount, int(perBin[k].size()));
      for (int j = 0; j < kBins; ++j) {
        if (!ok[j] || int(perBin[j].size()) * 5 < maxCount) continue;  // confident bins only
        for (int k = 0; k < kBins; ++k) {
          const float ratio = std::max(centre(k), centre(j)) / std::min(centre(k), centre(j));
          prof.sigma[b][c][k] = std::min(prof.sigma[b][c][k], prof.sigma[b][c][j] * std::pow(ratio, kMaxSlope));
        }
      }
      if (stats)
        for (int k = 0; k < kBins; ++k) stats->binMeasured[b][c][k] = ok[k];
    }
    // Correlation from blocks with low normalised variance in all channels.
    std::vector<std::pair<float, const Block*>> score;
    for (const Block& k : bl) {
      float m = 0;
      for (int c = 0; c < 3; ++c) {
        const float s = prof.at(b, c, k.value[c]);
        m = std::max(m, s > 0.f ? k.var[c] / (s * s) : 1e9f);
      }
      score.push_back({m, &k});
    }
    std::sort(score.begin(), score.end(), [](const auto& a, const auto& z) { return a.first < z.first; });
    const size_t use = std::max<size_t>(1, score.size() / 5);
    const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (int k = 0; k < 3; ++k) {
      double cv = 0, va = 0, vb = 0;
      for (size_t i = 0; i < use && i < score.size(); ++i) {
        const Block& q = *score[i].second;
        cv += q.cov[k]; va += q.var[pairs[k][0]]; vb += q.var[pairs[k][1]];
      }
      prof.corr[b][k] = va > 0 && vb > 0 ? float(std::clamp(cv / std::sqrt(va * vb), -1.0, 1.0)) : 0.f;
    }
  }
  // Across bands (not for the single pixel band): camera noise falls off smoothly towards coarser
  // bands, while coarse bands of real pictures are full of structure (texture only ever raises an
  // estimate). The ratio band1/band0 is measured reliably (many blocks); bands 2+ are capped at
  // the previous band times that ratio (x1.15, clamped 0.3..0.8). Measured on skin at 192x108:
  // band 4 was 18x over the truth before, within 0.7x after.
  if (domain != Domain::Pixel && int(bands.size()) >= 3)
    for (int c = 0; c < 3; ++c)
      for (int k = 0; k < kBins; ++k) {
        const float s0 = prof.sigma[0][c][k], s1 = prof.sigma[1][c][k];
        if (s0 <= 0.f) continue;
        const float cap = std::clamp(s1 / s0 * 1.15f, 0.3f, 0.8f);
        for (int b = 2; b < kBands && b < int(bands.size()); ++b)
          prof.sigma[b][c][k] = std::min(prof.sigma[b][c][k], prof.sigma[b - 1][c][k] * cap);
      }
  return prof;
}

}  // namespace

NoiseProfile estimateProfile(const vv::Plane& noisy, EstimateStats* stats, Domain domain) {
  return estimateImpl(noisy, noisy, stats, domain);
}

NoiseProfile estimateProfileTemporal(const vv::Plane& a, const vv::Plane& b, EstimateStats* stats, Domain domain) {
  // (a - b) / sqrt2 has the noise of one frame wherever the scene is static; the picture itself
  // cancels, so texture no longer looks like noise. Moving areas look like strong "texture" and are
  // rejected by the same robust statistics. Values for binning: (a + b) / 2.
  vv::Plane d = a, m = a;
  for (size_t i = 0; i < d.v.size(); ++i) {
    d.v[i] = (a.v[i] - b.v[i]) * 0.70710678f;
    m.v[i] = (a.v[i] + b.v[i]) * 0.5f;
  }
  return estimateImpl(d, m, stats, domain);
}

}  // namespace dn
