// Mapping between host parameter values and the core FilmParams. Shared by the CPU entry point
// and the GPU entry point so both read parameters identically. Header-only, no SDK types.
#pragma once
#include <cstdint>

#include "FilmEffects.h"
#include "params/params.h"

namespace fe {

/** Host-neutral parameter accessor: index -> value (float, bool or 1-based popup index). */
struct ParamReader {
  virtual ~ParamReader() = default;
  virtual double number(int index) const = 0;
  virtual bool flag(int index) const = 0;
  virtual int popup(int index) const = 0;  // 1-based
};

inline FilmParams readParams(const ParamReader& r) {
  FilmParams p;
  p.masterEnable = r.flag(FE_MASTER_ENABLE);
  p.mix = static_cast<float>(r.number(FE_MIX) / 100.0);
  p.space = static_cast<WorkingSpace>(r.popup(FE_WORKING_SPACE) - 1);
  p.quality = r.popup(FE_QUALITY) == 2 ? Quality::High : Quality::Normal;
  p.bloom.enabled = r.flag(FE_BLOOM_ENABLE);
  p.bloom.intensity = static_cast<float>(r.number(FE_BLOOM_INTENSITY));
  p.bloom.threshold = static_cast<float>(r.number(FE_BLOOM_THRESHOLD));
  p.bloom.knee = static_cast<float>(r.number(FE_BLOOM_KNEE));
  p.bloom.radius = static_cast<float>(r.number(FE_BLOOM_RADIUS));
  p.halation.enabled = r.flag(FE_HAL_ENABLE);
  p.halation.intensity = static_cast<float>(r.number(FE_HAL_INTENSITY));
  p.halation.threshold = static_cast<float>(r.number(FE_HAL_THRESHOLD));
  p.halation.radius = static_cast<float>(r.number(FE_HAL_RADIUS));
  p.halation.warmth = static_cast<float>(r.number(FE_HAL_WARMTH));
  p.grain.enabled = r.flag(FE_GRAIN_ENABLE);
  p.grain.amount = static_cast<float>(r.number(FE_GRAIN_AMOUNT));
  p.grain.size = static_cast<float>(r.number(FE_GRAIN_SIZE));
  p.grain.roughness = static_cast<float>(r.number(FE_GRAIN_ROUGHNESS));
  p.grain.shadows = static_cast<float>(r.number(FE_GRAIN_SHADOWS));
  p.grain.midtones = static_cast<float>(r.number(FE_GRAIN_MIDTONES));
  p.grain.highlights = static_cast<float>(r.number(FE_GRAIN_HIGHLIGHTS));
  p.grain.color = r.popup(FE_GRAIN_COLOR) == 2 ? GrainColor::Color : GrainColor::Luma;
  p.grain.chroma = static_cast<float>(r.number(FE_GRAIN_CHROMA));
  p.grain.seed = static_cast<int32_t>(r.number(FE_GRAIN_SEED));
  p.grain.animate = r.flag(FE_GRAIN_ANIMATE);
  return sanitized(p);
}

/** Stable per-frame grain key from a host time and frame duration (same units). Exact frame index
 *  when the time is frame-aligned; otherwise a distinct key derived from both values, so
 *  fractional/remapped times never collide with neighbouring frames. */
inline int64_t frameKeyFromTime(int64_t time, int64_t frameDuration) {
  if (frameDuration > 0 && time % frameDuration == 0) return time / frameDuration;
  uint64_t h = static_cast<uint64_t>(time) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(frameDuration);
  return static_cast<int64_t>(h | (1ull << 62));  // tagged so it never equals a small frame index
}

}  // namespace fe
