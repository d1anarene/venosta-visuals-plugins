#include "dn_temporal.h"

#include <algorithm>
#include <cmath>

#include "vv/blur.h"
#include "vv/parallel.h"

namespace dn {
namespace {

int binOf(float v) { return std::clamp(int((std::isfinite(v) ? v : 0.f) * kBins), 0, kBins - 1); }

vv::Plane lumaOf(const vv::Image& img) {
  vv::Plane l(img.width, img.height, 1);
  for (size_t i = 0, n = size_t(img.width) * img.height; i < n; ++i)
    l.v[i] = (img.px[i * 4] + img.px[i * 4 + 1] + img.px[i * 4 + 2]) * (1.f / 3.f);
  return l;
}

float sampleClamp(const vv::Plane& p, int x, int y) {
  return p.at(std::clamp(x, 0, p.width - 1), std::clamp(y, 0, p.height - 1))[0];
}

// Sum of absolute differences of an 8x8 block at (bx, by) in `a` against `b` displaced by (dx, dy).
float sad(const vv::Plane& a, const vv::Plane& b, int bx, int by, int dx, int dy) {
  float s = 0;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) s += std::fabs(sampleClamp(a, bx + x, by + y) - sampleClamp(b, bx + x + dx, by + y + dy));
  return s;
}

}  // namespace

float PixelSigma::at(int c, float v) const {
  const float* row = sigma[c];
  const float t = (std::isfinite(v) ? v : 0.f) * kBins - 0.5f;
  if (t <= 0.f) return row[0];
  if (t >= kBins - 1) return row[kBins - 1];
  const int i = int(t);
  return row[i] + (row[i + 1] - row[i]) * (t - float(i));
}

PixelSigma measurePixelSigma(const vv::Plane& noisy, const vv::Plane& clean) {
  PixelSigma ps;
  double s2[3][kBins] = {};
  long n[3][kBins] = {};
  for (size_t i = 0, m = size_t(noisy.width) * noisy.height; i < m; ++i)
    for (int c = 0; c < 3; ++c) {
      const double d = double(noisy.v[i * 3 + c]) - clean.v[i * 3 + c];
      const int k = binOf(clean.v[i * 3 + c]);
      s2[c][k] += d * d; ++n[c][k];
    }
  for (int c = 0; c < 3; ++c) {
    int last = -1;
    for (int k = 0; k < kBins; ++k)
      if (n[c][k] >= 32) { ps.sigma[c][k] = float(std::sqrt(s2[c][k] / double(n[c][k]))); last = k; }
    // Fill empty bins from the nearest measured one (constant extrapolation).
    for (int k = 0; k < kBins; ++k) {
      if (n[c][k] >= 32) continue;
      int best = -1;
      for (int d = 1; d < kBins && best < 0; ++d) {
        if (k - d >= 0 && n[c][k - d] >= 32) best = k - d;
        else if (k + d < kBins && n[c][k + d] >= 32) best = k + d;
      }
      ps.sigma[c][k] = best >= 0 ? ps.sigma[c][best] : (last >= 0 ? ps.sigma[c][last] : 0.f);
    }
  }
  return ps;
}

vv::Plane estimateMotion(const vv::Plane& from, const vv::Plane& to, int radius) {
  constexpr int kLevels = 3, kStep = 4;
  std::vector<vv::Plane> pf{from}, pt{to};
  for (int l = 1; l < kLevels; ++l) { pf.push_back(vv::downsample2(pf.back())); pt.push_back(vv::downsample2(pt.back())); }
  vv::Plane field;  // grid field of the previous (coarser) level, 2 channels, in that level's px
  int prevGw = 0, prevGh = 0;
  for (int l = kLevels - 1; l >= 0; --l) {
    const vv::Plane& a = pf[size_t(l)];
    const vv::Plane& b = pt[size_t(l)];
    const int gw = (a.width + kStep - 1) / kStep, gh = (a.height + kStep - 1) / kStep;
    vv::Plane cur(gw, gh, 2);
    vv::parallelRows(gh, [&](int gy) {
      for (int gx = 0; gx < gw; ++gx) {
        const int bx = gx * kStep - 2, by = gy * kStep - 2;  // 8x8 block centred on the grid point
        float px = 0, py = 0;
        if (prevGw > 0) {  // predictor from the coarser level (x2), nearest grid point
          const int cx = std::clamp(gx / 2, 0, prevGw - 1), cy = std::clamp(gy / 2, 0, prevGh - 1);
          px = 2.f * field.at(cx, cy)[0];
          py = 2.f * field.at(cx, cy)[1];
        }
        const int ix = int(std::lround(px)), iy = int(std::lround(py));
        const int r = l == kLevels - 1 ? radius : std::max(1, radius / 2);
        float best = sad(a, b, bx, by, ix, iy) * 0.98f;  // slight preference for the predictor
        int bdx = ix, bdy = iy;
        if (std::fabs(sad(a, b, bx, by, 0, 0)) < best) { best = sad(a, b, bx, by, 0, 0); bdx = 0; bdy = 0; }
        for (int dy = -r; dy <= r; ++dy)
          for (int dx = -r; dx <= r; ++dx) {
            const float c = sad(a, b, bx, by, ix + dx, iy + dy);
            if (c < best) { best = c; bdx = ix + dx; bdy = iy + dy; }
          }
        float fx = float(bdx), fy = float(bdy);
        if (l == 0) {  // parabolic sub-pixel refinement
          const float c0 = best;
          const float xm = sad(a, b, bx, by, bdx - 1, bdy), xp = sad(a, b, bx, by, bdx + 1, bdy);
          const float ym = sad(a, b, bx, by, bdx, bdy - 1), yp = sad(a, b, bx, by, bdx, bdy + 1);
          const float dxn = xm - 2 * c0 + xp, dyn = ym - 2 * c0 + yp;
          if (dxn > 1e-9f) fx += std::clamp(0.5f * (xm - xp) / dxn, -0.5f, 0.5f);
          if (dyn > 1e-9f) fy += std::clamp(0.5f * (ym - yp) / dyn, -0.5f, 0.5f);
        }
        cur.at(gx, gy)[0] = fx;
        cur.at(gx, gy)[1] = fy;
      }
    });
    field = std::move(cur);
    prevGw = gw; prevGh = gh;
  }
  // Dense field at full resolution: bilinear interpolation of the grid (grid point g at g*kStep+2).
  vv::Plane dense(from.width, from.height, 2);
  for (int y = 0; y < from.height; ++y)
    for (int x = 0; x < from.width; ++x) {
      const float gx = std::clamp((float(x) - 2.f) / kStep, 0.f, float(prevGw - 1));
      const float gy = std::clamp((float(y) - 2.f) / kStep, 0.f, float(prevGh - 1));
      const int x0 = int(gx), y0 = int(gy), x1 = std::min(x0 + 1, prevGw - 1), y1 = std::min(y0 + 1, prevGh - 1);
      const float tx = gx - float(x0), ty = gy - float(y0);
      for (int c = 0; c < 2; ++c) {
        const float a0 = field.at(x0, y0)[c] + (field.at(x1, y0)[c] - field.at(x0, y0)[c]) * tx;
        const float a1 = field.at(x0, y1)[c] + (field.at(x1, y1)[c] - field.at(x0, y1)[c]) * tx;
        dense.at(x, y)[c] = a0 + (a1 - a0) * ty;
      }
    }
  return dense;
}

vv::Image denoiseTemporal(const std::vector<const vv::Image*>& frames, int centre, const NoiseProfile& prof,
                          const PixelSigma& ps, const SpatialParams& sp, const TemporalParams& tp, TemporalDebug* dbg,
                          const std::vector<vv::Image>* pilotsIn) {
  const vv::Image& c = *frames[size_t(centre)];
  const int w = c.width, h = c.height;
  std::vector<vv::Image> own;
  if (!pilotsIn) for (const vv::Image* f : frames) own.push_back(denoiseSpatial(*f, prof, sp));
  const std::vector<vv::Image>& pilots = pilotsIn ? *pilotsIn : own;
  const vv::Plane lc = lumaOf(pilots[size_t(centre)]);
  vv::Plane sum(w, h, 3), wsum(w, h, 1), wsq(w, h, 1);
  for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
    for (int k = 0; k < 3; ++k) sum.v[i * 3 + k] = c.px[i * 4 + k];
    wsum.v[i] = 1.f;
    wsq.v[i] = 1.f;
  }
  double weightAcc = 0;
  int neighbours = 0;
  for (size_t f = 0; f < frames.size(); ++f) {
    if (int(f) == centre) continue;
    const vv::Image& nb = *frames[f];
    const vv::Plane flow = estimateMotion(lc, lumaOf(pilots[f]), tp.searchRadius);
    // Warp the noisy neighbour; record the interpolation's noise reduction factor.
    vv::Plane warped(w, h, 3), interp(w, h, 1), z(w, h, 1);
    vv::parallelRows(h, [&](int y) {
      for (int x = 0; x < w; ++x) {
        const float sx = std::clamp(float(x) + flow.at(x, y)[0], 0.f, float(w - 1));
        const float sy = std::clamp(float(y) + flow.at(x, y)[1], 0.f, float(h - 1));
        const int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const float tx = sx - float(x0), ty = sy - float(y0);
        for (int k = 0; k < 3; ++k) {
          const float a = nb.at(x0, y0)[k] + (nb.at(x1, y0)[k] - nb.at(x0, y0)[k]) * tx;
          const float b = nb.at(x0, y1)[k] + (nb.at(x1, y1)[k] - nb.at(x0, y1)[k]) * tx;
          warped.at(x, y)[k] = a + (b - a) * ty;
        }
        interp.at(x, y)[0] = ((1 - tx) * (1 - tx) + tx * tx) * ((1 - ty) * (1 - ty) + ty * ty);
        float e = 0;
        for (int k = 0; k < 3; ++k) {
          const float d = c.at(x, y)[k] - warped.at(x, y)[k];
          const float s = ps.at(k, c.at(x, y)[k]);
          const float var = std::max(s * s * (1.f + interp.at(x, y)[0]), 1e-12f);
          e += d * d / var;
        }
        z.at(x, y)[0] = e / 3.f;
      }
    });
    const vv::Plane zs = vv::gaussianBlurDirect(z, 1.2f);  // ~5x5 window
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
      const float wt = std::exp(-tp.gain * std::max(zs.v[i] - tp.slack, 0.f));
      for (int k = 0; k < 3; ++k) sum.v[i * 3 + k] += wt * warped.v[i * 3 + k];
      wsum.v[i] += wt;
      wsq.v[i] += wt * wt * interp.v[i];
      weightAcc += wt;
    }
    ++neighbours;
  }
  vv::Image fused = c;
  vv::Plane scale(w, h, 1);
  for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
    for (int k = 0; k < 3; ++k) fused.px[i * 4 + k] = sum.v[i * 3 + k] / wsum.v[i];
    scale.v[i] = std::sqrt(wsq.v[i]) / wsum.v[i];  // noise std after the weighted average
  }
  if (dbg) dbg->meanWeight = neighbours ? float(weightAcc / (double(neighbours) * w * h)) : 0.f;
  if (tp.postStrength <= 0.f) return fused;
  SpatialParams post = sp;
  post.lumaStrength *= tp.postStrength;
  post.chromaStrength *= tp.postStrength;
  return denoiseSpatial(fused, prof, post, &scale);
}

}  // namespace dn
