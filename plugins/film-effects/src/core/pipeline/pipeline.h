// Film Effects processing pipeline (CPU reference).
//
// Order (documented in docs/ARCHITECTURE.md):
//   input -> linearise -> bloom energy (from input) + halation spill (from input) -> back to
//   working encoding -> grain (encoded domain) -> global mix -> alpha restored.
// Bloom and halation are both derived from the ORIGINAL input, never from each other, so neither
// is applied twice and their order does not matter.
#pragma once
#include <cstdint>

#include "../image.h"
#include "../params/params.h"

namespace fe {

struct RenderContext {
  /** Stable per-frame key from host time (e.g. clip time / frame duration). Ignored when
   *  grain.animate is off. */
  int64_t frameKey = 0;
};

/** Per-effect intermediate results, exposed for tests and diagnostics. */
struct Layers {
  Plane bloomAdd;     // linear RGB energy added by bloom (empty when bloom is off)
  Plane halationAdd;  // linear RGB energy added by halation (empty when off)
};

/** Process `img` in place. Alpha is never modified. Returns false (and leaves img untouched)
 *  when the image is empty. With all effects off/mix 0 the image is not touched at all. */
bool process(Image& img, const FilmParams& params, const RenderContext& ctx, Layers* debug = nullptr);

// Individual stages (used by process() and by tests).
Plane linearRGB(const Image& img, WorkingSpace space);
Plane bloomEnergy(const Plane& lin, const BloomParams& p, Quality q);
Plane halationEnergy(const Plane& lin, const HalationParams& p);
/** Grain field value (approx. unit variance) for pixel (x, y) of an image of given height. */
float grainSample(int x, int y, int imageHeight, const GrainParams& g, int64_t frameKey, int channel);

}  // namespace fe
