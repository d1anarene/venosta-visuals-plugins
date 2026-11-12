#include "lens.h"

#include <algorithm>
#include <cmath>

#include "ld_kernels.h"
#include "vv/blur.h"
#include "vv/parallel.h"

namespace ld {

DiffusionPsf diffusionPsf(const DiffusionParams& d, int height) {
  const float r = d.radius * 0.01f * static_cast<float>(height);  // widest sigma in pixels
  DiffusionPsf p{{0.12f * r, 0.4f * r, r}, {1.0f - d.halo, 0.6f * d.halo, 0.4f * d.halo}};
  // A small core is always present, so halo = 1 still softens a little near the source.
  p.weight[0] = std::max(p.weight[0], 0.05f);
  const float sum = p.weight[0] + p.weight[1] + p.weight[2];
  for (float& w : p.weight) w /= sum;
  return p;
}

StreakProfile streakProfile(const StreakParams& s, int width) {
  const float l = s.length * 0.01f * static_cast<float>(width);
  return StreakProfile{{0.06f * l, 0.2f * l, 0.5f * l}, {0.5f, 0.3f, 0.2f}};
}

namespace {

Plane linearise(const Image& img, int space) {
  Plane lin(img.width, img.height, 3);
  vv::parallelRows(img.height, [&](int y) {
    for (int x = 0; x < img.width; ++x)
      for (int c = 0; c < 3; ++c) lin.at(x, y)[c] = vv_to_linear1(img.at(x, y)[c], space);
  });
  return lin;
}

// Red and blue resampled (Catmull-Rom) at radially displaced positions; green unchanged.
Plane aberrate(const Plane& lin, const AberrationParams& a) {
  const int w = lin.width, h = lin.height;
  const float cx = w * 0.5f, cy = h * 0.5f;
  const float rc = std::sqrt(cx * cx + cy * cy);
  const float cornerPx = a.amount * 0.5f * static_cast<float>(h) / 1080.0f;  // each of R, B: half
  Plane out(w, h, 3);
  vv::parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      const float vx = x + 0.5f - cx, vy = y + 0.5f - cy;
      const float r = std::sqrt(vx * vx + vy * vy);
      out.at(x, y)[1] = lin.at(x, y)[1];
      for (int c = 0; c < 3; c += 2) {
        const float sgn = c == 0 ? 1.0f : -1.0f;  // red outward, blue inward (amount > 0)
        const float shift = sgn * ld_aberration_shift(r / rc, cornerPx, a.edgeBias);
        const float k = r > 1e-6f ? 1.0f - shift / r : 1.0f;  // sample closer to the centre = magnify
        const float sx = cx + vx * k - 0.5f, sy = cy + vy * k - 0.5f;
        const float bx = std::floor(sx), by = std::floor(sy), tx = sx - bx, ty = sy - by;
        float acc = 0.f;
        for (int j = 0; j < 4; ++j) {
          const int yy = std::clamp(static_cast<int>(by) - 1 + j, 0, h - 1);
          const float wy = vv_catmull_rom(ty, j);
          for (int i = 0; i < 4; ++i) {
            const int xx = std::clamp(static_cast<int>(bx) - 1 + i, 0, w - 1);
            acc += wy * vv_catmull_rom(tx, i) * lin.at(xx, yy)[c];
          }
        }
        out.at(x, y)[c] = acc;
      }
    }
  });
  return out;
}

void addScaled(Plane& acc, const Plane& v, float w) {
  for (size_t i = 0; i < acc.v.size(); ++i) acc.v[i] += w * v.v[i];
}

}  // namespace

bool process(Image& img, const LensParams& raw) {
  if (img.empty()) return false;
  const LensParams p = sanitized(raw);
  if (!p.anyEffectActive()) return true;  // bypass: pixels untouched
  const int sp = static_cast<int>(p.space);
  const int w = img.width, h = img.height;

  Plane lin = linearise(img, sp);
  if (p.aberration.enabled && p.aberration.amount != 0.f) lin = aberrate(lin, p.aberration);
  const Plane source = lin;  // scene light after the lens' colour separation

  if (p.diffusion.enabled && p.diffusion.amount > 0.f) {
    Plane s(w, h, 3);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
      const float* c = &source.v[i * 3];
      const float wgt = ld_diffusion_weight(vv_luma(vv_make3(std::max(c[0], 0.f), std::max(c[1], 0.f), std::max(c[2], 0.f))),
                                            p.diffusion.highlightBias);
      for (int k = 0; k < 3; ++k) s.v[i * 3 + k] = std::max(c[k], 0.f) * wgt;
    }
    const DiffusionPsf psf = diffusionPsf(p.diffusion, h);
    Plane scattered(w, h, 3);
    for (int k = 0; k < 3; ++k)
      addScaled(scattered, vv::blurByPlan(s, vv::planGaussian(psf.sigma[k], w, h)), psf.weight[k]);
    const vv_float3 t = ld_warmth_tint(p.diffusion.warmth);
    const float tint[3] = {t.x, t.y, t.z};
    const float a = p.diffusion.amount;
    for (size_t i = 0; i < size_t(w) * h; ++i)
      for (int k = 0; k < 3; ++k) lin.v[i * 3 + k] += a * (scattered.v[i * 3 + k] * tint[k] - s.v[i * 3 + k]);
  }

  if (p.streaks.enabled && p.streaks.intensity > 0.f) {
    Plane b(w, h, 3);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
      const float* c = &source.v[i * 3];
      const float cc[3] = {std::max(c[0], 0.f), std::max(c[1], 0.f), std::max(c[2], 0.f)};
      const float wgt = vv_soft_knee_weight(vv_luma(vv_make3(cc[0], cc[1], cc[2])), p.streaks.threshold, p.streaks.knee);
      for (int k = 0; k < 3; ++k) b.v[i * 3 + k] = cc[k] * wgt;
    }
    const StreakProfile prof = streakProfile(p.streaks, w);
    Plane streak(w, h, 3);
    for (int k = 0; k < 3; ++k) addScaled(streak, vv::gaussianBlurHorizontal(b, prof.sigma[k]), prof.weight[k]);
    const vv_float3 t = ld_streak_tint(static_cast<int>(p.streaks.color));
    const float tint[3] = {t.x, t.y, t.z};
    for (size_t i = 0; i < size_t(w) * h; ++i)
      for (int k = 0; k < 3; ++k) lin.v[i * 3 + k] += p.streaks.intensity * tint[k] * streak.v[i * 3 + k];
  }

  const bool doVignette = p.vignette.enabled && p.vignette.amount != 0.f;
  const float mix = p.mix;
  vv::parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      float* px = img.at(x, y);
      if (px[3] <= 0.f) continue;  // never invent colour where the layer is fully transparent
      float g = 1.f;
      if (doVignette) {
        const float d = ld_vignette_distance(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                             static_cast<float>(h), p.vignette.roundness);
        g = ld_vignette_gain(d, p.vignette.amount, p.vignette.size, p.vignette.feather,
                             static_cast<int>(p.vignette.style));
      }
      const float* l = lin.at(x, y);
      for (int c = 0; c < 3; ++c) {
        const float o = vv_from_linear1(l[c] * g, sp);
        const float v = mix >= 1.f ? o : px[c] + (o - px[c]) * mix;
        if (std::isfinite(v)) px[c] = v;
      }
    }
  });
  return true;
}

}  // namespace ld
