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

  const Image original = img;
  const int sp = static_cast<int>(p.space);
  const bool doBloom = p.bloom.enabled && p.bloom.intensity > 0.f;
  const bool doHal = p.halation.enabled && p.halation.intensity > 0.f;
  const bool doGrain = p.grain.enabled && p.grain.amount > 0.f;
  const size_t n = static_cast<size_t>(img.width) * img.height;

  if (doBloom || doHal) {
    Plane lin = linearRGB(img, p.space);
    Plane bloom = doBloom ? bloomEnergy(lin, p.bloom, p.quality) : Plane();
    Plane hal = doHal ? halationEnergy(lin, p.halation) : Plane();
    for (size_t i = 0; i < n; ++i) {
      for (int c = 0; c < 3; ++c) {
        float add = (doBloom ? bloom.v[i * 3 + c] : 0.f) + (doHal ? hal.v[i * 3 + c] : 0.f);
        if (add != 0.0f) img.px[i * 4 + c] = fe_from_linear1(lin.v[i * 3 + c] + add, sp);
      }
    }
    if (debug) {
      debug->bloomAdd = std::move(bloom);
      debug->halationAdd = std::move(hal);
    }
  }

  if (doGrain) {
    const GrainParams& g = p.grain;
    // Amount 1.0 == standard deviation 0.12 in working-space units at full response.
    const float amp = g.amount * 0.12f;
    // Footprint: when granules get smaller than a render pixel (low-res preview), the visible
    // variance of area-averaged grain drops; approximate that instead of aliasing to white noise.
    const float sizePx = g.size * img.height / 1080.0f;
    const float footprint = sizePx < 1.0f ? sizePx : 1.0f;
    parallelRows(img.height, [&](int y) {
      for (int x = 0; x < img.width; ++x) {
        float* px = img.at(x, y);
        float lum = fe_luma(fe_make3(px[0], px[1], px[2]));
        float a = amp * footprint * fe_grain_response(lum, g.shadows, g.midtones, g.highlights);
        if (a <= 0.f) continue;
        float base = grainSample(x, y, img.height, g, ctx.frameKey, 0);
        if (g.color == GrainColor::Luma) {
          for (int c = 0; c < 3; ++c) px[c] += a * base;
        } else {
          for (int c = 0; c < 3; ++c) {
            float ch = grainSample(x, y, img.height, g, ctx.frameKey, c + 1);
            px[c] += a * (base + g.chroma * ch);
          }
        }
      }
    });
  }

  const float mix = p.mix;
  for (size_t i = 0; i < n; ++i) {
    float* o = &img.px[i * 4];
    const float* s = &original.px[i * 4];
    if (s[3] <= 0.0f) {  // never invent colour where the layer is fully transparent
      for (int c = 0; c < 4; ++c) o[c] = s[c];
      continue;
    }
    for (int c = 0; c < 3; ++c) {
      float v = mix >= 1.0f ? o[c] : s[c] + (o[c] - s[c]) * mix;
      o[c] = std::isfinite(v) ? v : s[c];
    }
    o[3] = s[3];
  }
  return true;
}

}  // namespace fe
