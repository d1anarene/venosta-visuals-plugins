#include <cmath>

#include "../common/fe_kernels.h"
#include "../pipeline/pipeline.h"

namespace fe {

// Grain coordinates are full-frame pixel positions; granule size is defined relative to a
// 1080-line frame, so a 4K export and an HD export show the same apparent grain at the same
// display size, and a half-resolution preview shows the same pattern (sampled more coarsely).
float grainSample(int x, int y, int imageHeight, const GrainParams& g, int64_t frameKey, int channel) {
  const float scale = static_cast<float>(imageHeight) / 1080.0f;
  const float sizePx = g.size * scale;
  const uint64_t t = g.animate ? static_cast<uint64_t>(frameKey) : 0u;
  fe_uint key = fe_hash3(static_cast<fe_uint>(g.seed), static_cast<fe_uint>(t & 0xffffffffu),
                         static_cast<fe_uint>(t >> 32));
  key = fe_hash(key + static_cast<fe_uint>(channel) * 0x632BE5ABu);
  return fe_grain_field(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, sizePx, g.roughness, key);
}

}  // namespace fe
