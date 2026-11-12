// Film Gate — per-frame values and the CPU reference pass.
//
// Model (what happens to film in a projector or scanner gate):
//   film image (bloom, halation, grain already applied; dust and scratches are ON the film)
//     -> moves in the gate by the weave offset (sub-pixel, Catmull-Rom resampling)
//     -> lamp flicker (exposure gain, linear light)
//     -> gate aperture (rounded, soft edge; fixed to the frame, black outside)
//     -> global mix against the untouched source
// Everything is a pure function of (seed, frame key, frame size, parameters): any frame renders
// identically in any order, on any thread, on CPU or GPU.
#pragma once
#include <cstdint>

#include "../image.h"
#include "../params/params.h"

namespace fe {

constexpr int kMaxScratches = 4;

struct GateScratch {
  float x0 = 0, slope = 0, widthPx = 1, strength = 0;
  uint32_t key = 0;
  int32_t sign = -1;  // -1 dark, +1 light
};

/** Everything the per-pixel gate pass needs for one frame (also uploaded to the GPU). */
struct GateFrame {
  bool active = false;
  bool doWeave = false, doDefects = false, doEdge = false;
  float dx = 0, dy = 0;  // weave offset in pixels (film moves by +dx, +dy; y down)
  float gain = 1;        // flicker exposure gain (linear)
  float insetPx = 0, softPx = 0.5f, radiusPx = 0;
  float dustCellPx = 32, dustDensity = 0, dustSizePx = 2;
  uint32_t dustKey = 0;
  int32_t look = 0;
  int32_t scratchCount = 0;
  GateScratch scratches[kMaxScratches];
};

/** Per-frame values for a `width` x `height` frame at `frameKey` (see frameKeyFromTime). */
GateFrame gateFrame(const GateParams& g, int width, int height, int64_t frameKey);

/** Signed defect (dust + scratches) at film position (x, y), top row = 0. */
float gateDefect(const GateFrame& f, float x, float y, int height);

/** CPU gate pass: dst(x, y) = gate(film) mixed with src by `mix`. `film` and `src` are top-down
 *  RGBA images of the same size; alpha is taken from `src` unchanged. src and dst may be the same. */
void applyGate(const Image& film, const Image& src, Image& dst, const GateFrame& f, int space, float mix);

}  // namespace fe
