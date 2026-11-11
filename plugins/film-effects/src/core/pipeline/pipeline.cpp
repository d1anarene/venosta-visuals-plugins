#include "pipeline.h"

#include <cmath>

#include "../common/fe_kernels.h"
#include "../common/parallel.h"

namespace fe {

Plane linearRGB(const Image& img, WorkingSpace space) {
  Plane lin(img.width, img.height, 3);
  const int sp = static_cast<int>(space);
  for (size_t i = 0; i < static_cast<size_t>(img.width) * img.height; ++i) {
    for (int c = 0; c < 3; ++c) lin.v[i * 3 + c] = fe_to_linear1(img.px[i * 4 + c], sp);
  }
  return lin;
}

bool process(Image& img, const FilmParams& raw, const RenderContext& ctx, Layers* debug) {
  if (img.empty()) return false;
  const FilmParams p = sanitized(raw);
  if (!p.anyEffectActive()) return true;  // bypass: pixels untouched

  const int sp = static_cast<int>(p.space);
  const bool doBloom = p.bloom.enabled && p.bloom.intensity > 0.f;
  const bool doHal = p.halation.enabled && p.halation.intensity > 0.f;
  const bool doGrain = p.grain.enabled && p.grain.amount > 0.f;

  // Spatial stages read the unmodified input image.
  Plane bloom = doBloom ? bloomEnergy(img, p.space, p.bloom, p.quality) : Plane();
  Plane spill = doHal ? halationSpill(img, p.space, p.halation) : Plane();
  const fe_float3 tint = fe_halation_tint(p.halation.warmth);

  const GrainParams& g = p.grain;
  // Amount 1.0 == standard deviation 0.12 in working-space units at full response.
  const float amp = g.amount * 0.12f;
  // Footprint: when granules get smaller than a render pixel (low-res preview), the visible
  // variance of area-averaged grain drops; approximate that instead of aliasing to white noise.
  const float sizePx = g.size * img.height / 1080.0f;
  const float footprint = sizePx < 1.0f ? sizePx : 1.0f;
  const float mix = p.mix;

  // Single per-pixel pass, in place: each pixel's original value is read before it is written,
  // so no copy of the frame is needed.
  parallelRows(img.height, [&](int y) {
    for (int x = 0; x < img.width; ++x) {
      float* px = img.at(x, y);
      const size_t i = static_cast<size_t>(y) * img.width + x;
      const float orig[4] = {px[0], px[1], px[2], px[3]};
      if (orig[3] <= 0.0f) continue;  // never invent colour where the layer is fully transparent
      float o[3] = {orig[0], orig[1], orig[2]};
      if (doBloom || doHal) {
        const float tintc[3] = {tint.x, tint.y, tint.z};
        for (int c = 0; c < 3; ++c) {
          float add = (doBloom ? bloom.v[i * 3 + c] : 0.f) + (doHal ? spill.v[i] * tintc[c] : 0.f);
          if (add != 0.0f) o[c] = fe_from_linear1(fe_to_linear1(orig[c], sp) + add, sp);
        }
      }
      if (doGrain) {
        float lum = fe_luma(fe_make3(o[0], o[1], o[2]));
        float a = amp * footprint * fe_grain_response(lum, g.shadows, g.midtones, g.highlights);
        if (a > 0.f) {
          float base = grainSample(x, y, img.height, g, ctx.frameKey, 0);
          if (g.color == GrainColor::Luma) {
            for (int c = 0; c < 3; ++c) o[c] += a * base;
          } else {
            for (int c = 0; c < 3; ++c) o[c] += a * (base + g.chroma * grainSample(x, y, img.height, g, ctx.frameKey, c + 1));
          }
        }
      }
      for (int c = 0; c < 3; ++c) {
        float v = mix >= 1.0f ? o[c] : orig[c] + (o[c] - orig[c]) * mix;
        px[c] = std::isfinite(v) ? v : orig[c];
      }
    }
  });

  if (debug) {
    debug->bloomAdd = std::move(bloom);
    if (doHal) {
      debug->halationAdd = Plane(img.width, img.height, 3);
      for (size_t i = 0; i < spill.v.size(); ++i) {
        debug->halationAdd.v[i * 3 + 0] = spill.v[i] * tint.x;
        debug->halationAdd.v[i * 3 + 1] = spill.v[i] * tint.y;
        debug->halationAdd.v[i * 3 + 2] = spill.v[i] * tint.z;
      }
    }
  }
  return true;
}

}  // namespace fe
