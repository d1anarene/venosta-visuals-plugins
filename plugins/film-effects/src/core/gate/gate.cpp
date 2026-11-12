#include "gate.h"

#include <algorithm>
#include <cmath>

#include "../common/fe_kernels.h"
#include "../common/parallel.h"

namespace fe {
namespace {

// Hashed value in [-1, 1] for integer n.
double signedHash(int64_t n, uint32_t key) {
  const uint64_t u = static_cast<uint64_t>(n);
  const fe_uint h = fe_hash3(static_cast<fe_uint>(u & 0xffffffffu), static_cast<fe_uint>(u >> 32), key);
  return fe_unit(h) * 2.0 - 1.0;
}

// Smooth 1-D noise in [-1, 1] over time t (in lattice units), quintic interpolation. Double
// precision on the host so long timelines keep full frame resolution.
double smoothNoise(double t, uint32_t key) {
  const double f = std::floor(t);
  const int64_t i = static_cast<int64_t>(f);
  const double u = t - f;
  const double s = u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
  return signedHash(i, key) * (1.0 - s) + signedHash(i + 1, key) * s;
}

// Time signal: mostly smooth drift with some frame-to-frame jitter, roughly within [-1, 1].
double drift(int64_t frame, double period, double jitter, uint32_t key) {
  return (1.0 - jitter) * smoothNoise(static_cast<double>(frame) / period, key) + jitter * signedHash(frame, key ^ 0x5bd1e995u);
}

}  // namespace

GateFrame gateFrame(const GateParams& raw, int width, int height, int64_t frameKey) {
  GateFrame f;
  if (!raw.anyActive() || width <= 0 || height <= 0) return f;
  const GateParams& g = raw;
  f.active = true;
  const float scale = static_cast<float>(height) / 1080.0f;
  const uint32_t seed = fe_hash(static_cast<fe_uint>(g.seed) * 0x9E3779B9u + 0x7f4a7c15u);

  // Weave: vertical (frame registration) stronger than horizontal, slow drift plus jitter.
  if (g.weave > 0.f) {
    const double amp = g.weave * scale;
    f.dx = static_cast<float>(amp * 0.6 * drift(frameKey, 5.0, 0.3, seed ^ 0x1u));
    f.dy = static_cast<float>(amp * drift(frameKey, 5.0, 0.3, seed ^ 0x2u));
    f.doWeave = std::fabs(f.dx) > 1e-4f || std::fabs(f.dy) > 1e-4f;
  }
  // Flicker: amount 1 = about +-20 % exposure.
  if (g.flicker > 0.f) {
    f.gain = static_cast<float>(std::max(0.0, 1.0 + 0.2 * g.flicker * drift(frameKey, 2.5, 0.4, seed ^ 0x3u)));
  }
  // Gate aperture.
  if (g.edge > 0.f) {
    f.doEdge = true;
    f.insetPx = g.edge * 0.01f * height;
    f.softPx = 0.5f + g.edgeSoftness * 0.02f * height;
    f.radiusPx = g.edgeRoundness * 0.12f * height;
  }
  // Dust: new specks every frame.
  const fe_uint frameHash = fe_hash3(static_cast<fe_uint>(static_cast<uint64_t>(frameKey) & 0xffffffffu),
                                     static_cast<fe_uint>(static_cast<uint64_t>(frameKey) >> 32), seed);
  f.look = static_cast<int32_t>(g.look);
  if (g.dust > 0.f) {
    f.dustSizePx = std::max(1.0f, 3.0f * scale);
    f.dustCellPx = std::max(4.0f * f.dustSizePx, 32.0f * scale);
    f.dustDensity = 0.06f * g.dust;
    f.dustKey = fe_hash(frameHash ^ 0xD1B54A32u);
    f.doDefects = true;
  }
  // Scratches: up to four slots; each scratch lives for a run of frames, then moves elsewhere.
  if (g.scratches > 0.f) {
    for (int s = 0; s < kMaxScratches; ++s) {
      const uint32_t slotKey = fe_hash(seed + 0x632BE5ABu * static_cast<uint32_t>(s + 1));
      const int64_t life = 6 + static_cast<int64_t>(fe_unit(slotKey) * 34.0f);  // 6..40 frames
      const int64_t offset = static_cast<int64_t>(fe_unit(fe_hash(slotKey)) * static_cast<float>(life));
      const int64_t t = frameKey + offset;
      const int64_t epoch = t >= 0 ? t / life : -((-t + life - 1) / life);
      const fe_uint h = fe_hash3(static_cast<fe_uint>(static_cast<uint64_t>(epoch) & 0xffffffffu),
                                 static_cast<fe_uint>(static_cast<uint64_t>(epoch) >> 32), slotKey);
      // Slot s is used when the amount reaches its threshold, and then present on part of its runs.
      if (g.scratches * kMaxScratches <= static_cast<float>(s) * 0.85f) continue;
      if (fe_unit(h) > 0.35f + 0.6f * g.scratches) continue;
      GateScratch& sc = f.scratches[f.scratchCount++];
      const float age = static_cast<float>(t - epoch * life);
      sc.x0 = (0.05f + 0.9f * fe_unit(fe_hash(h + 1u))) * width + (fe_unit(fe_hash(h + 2u)) - 0.5f) * 0.3f * scale * age;
      sc.slope = (fe_unit(fe_hash(h + 3u)) - 0.5f) * 0.02f;
      sc.widthPx = std::max(0.8f, (0.8f + 1.6f * fe_unit(fe_hash(h + 4u))) * scale);
      sc.strength = 0.35f + 0.65f * fe_unit(fe_hash(h + 5u));
      sc.key = fe_hash(h + 6u);
      sc.sign = g.look == DefectLook::Light ? 1 : (g.look == DefectLook::Dark ? -1 : ((h >> 7) & 1u ? 1 : -1));
    }
    if (f.scratchCount > 0) f.doDefects = true;
  }
  return f;
}

float gateDefect(const GateFrame& f, float x, float y, int height) {
  float d = 0.f;
  if (f.dustDensity > 0.f) d = fe_dust(x, y, f.dustCellPx, f.dustDensity, f.dustSizePx, f.dustKey, f.look);
  for (int s = 0; s < f.scratchCount; ++s) {
    const GateScratch& sc = f.scratches[s];
    d += static_cast<float>(sc.sign) * fe_scratch(x, y, static_cast<float>(height), sc.x0, sc.slope, sc.widthPx, sc.strength, sc.key);
  }
  return fe_clamp(d, -1.f, 1.f);
}

void applyGate(const Image& film, const Image& src, Image& dst, const GateFrame& f, int space, float mix) {
  const int w = film.width, h = film.height;
  parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      const float* s = src.at(x, y);
      float* d = dst.at(x, y);
      const float orig[4] = {s[0], s[1], s[2], s[3]};
      if (orig[3] <= 0.0f) {  // never invent colour where the layer is fully transparent
        for (int c = 0; c < 4; ++c) d[c] = orig[c];
        continue;
      }
      // Film position seen through this screen pixel.
      const float fx = static_cast<float>(x) - f.dx, fy = static_cast<float>(y) - f.dy;
      float col[3];
      if (f.doWeave) {
        const float bx = std::floor(fx), by = std::floor(fy);
        const float tx = fx - bx, ty = fy - by;
        col[0] = col[1] = col[2] = 0.f;
        for (int j = 0; j < 4; ++j) {
          const int sy = std::min(std::max(static_cast<int>(by) - 1 + j, 0), h - 1);
          const float wy = fe_catmull_rom(ty, j);
          for (int i = 0; i < 4; ++i) {
            const int sx = std::min(std::max(static_cast<int>(bx) - 1 + i, 0), w - 1);
            const float wgt = wy * fe_catmull_rom(tx, i);
            const float* p = film.at(sx, sy);
            col[0] += wgt * p[0]; col[1] += wgt * p[1]; col[2] += wgt * p[2];
          }
        }
      } else {
        const float* p = film.at(x, y);
        col[0] = p[0]; col[1] = p[1]; col[2] = p[2];
      }
      const float defect = f.doDefects ? gateDefect(f, fx, fy, h) : 0.f;
      const float mask = f.doEdge ? fe_gate_mask(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                                 static_cast<float>(h), f.insetPx, f.softPx, f.radiusPx)
                                  : 1.f;
      const fe_float3 o = fe_gate_shade(fe_make3(col[0], col[1], col[2]), defect, f.gain, mask, space);
      const float out[3] = {o.x, o.y, o.z};
      for (int c = 0; c < 3; ++c) {
        const float v = mix >= 1.0f ? out[c] : orig[c] + (out[c] - orig[c]) * mix;
        d[c] = std::isfinite(v) ? v : orig[c];
      }
      d[3] = orig[3];
    }
  });
}

}  // namespace fe
