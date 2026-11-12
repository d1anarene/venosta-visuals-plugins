// The interface between a plugin and the shared host adapters.
//
// A plugin implements vv::Effect once and defines vv::effect() to return it. The shared Premiere
// Pro adapter (shared/engine/hosts/premiere) and OpenFX adapter (shared/engine/hosts/ofx) do the
// rest: registration, parameters, pixel formats, GPU entry points, bypass.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "image.h"
#include "params.h"

namespace vv {

struct EffectInfo {
  const char* name;         // shown in the host's effects list
  const char* matchName;    // Premiere match name. NEVER change after release (projects use it)
  const char* ofxId;        // OpenFX identifier. NEVER change after release
  const char* category;     // effects folder, e.g. "Venosta Visuals"
  const char* description;  // one line
  int versionMajor = 1, versionMinor = 0;
  /** OpenFX parameter names are "<prefix><diskId>" and are saved in host projects: NEVER change
   *  after release. */
  const char* ofxParamPrefix = "p";
};

enum class GpuApi { Cuda, Metal };

/** One GPU frame handed over by a host. Pixels are float4 (32f) or half4 (16f) per pixel. */
struct GpuFrame {
  const void* src = nullptr;  // CUDA device pointer or id<MTLBuffer>
  void* dst = nullptr;        // may equal src (in place)
  bool half = false;          // 16-bit float frame
  uint32_t width = 0, height = 0, rowPixels = 0;
  bool rgba = false;          // channel order RGBA (OpenFX) instead of BGRA (Premiere)
  bool bottomUp = false;      // row 0 is the bottom row (OpenFX)
  void* queue = nullptr;      // Metal: id<MTLCommandQueue>; CUDA: host stream (may be null)
};

/** Per-instance GPU renderer. Must have finished the frame when render() returns. */
class GpuRenderer {
 public:
  virtual ~GpuRenderer() = default;
  /** Create GPU resources up front (queue: Metal command queue, or null). Returning false lets
   *  the host fall back to the CPU path (Premiere: instance creation fails -> software). */
  virtual bool prepare(void* /*queue*/) { return true; }
  virtual bool render(const GpuFrame& frame, const ParamReader& params, int64_t frameKey) = 0;
};

class Effect {
 public:
  virtual ~Effect() = default;
  virtual const EffectInfo& info() const = 0;
  /** Controls in panel order; host index = position + 1. */
  virtual const std::vector<ParamSpec>& params() const = 0;
  /** false: nothing to do, the host passes the frame through untouched (bit-exact bypass). */
  virtual bool active(const ParamReader& params) const = 0;
  /** CPU render, in place. `img` is straight-alpha RGBA float, top row first, in the host's
   *  working encoding. Alpha must not change. */
  virtual bool render(Image& img, const ParamReader& params, int64_t frameKey) const = 0;
  /** A supervised parameter changed (e.g. a preset menu). Return true if values were written. */
  virtual bool paramChanged(int /*hostIndex*/, const ParamReader& /*params*/, ParamWriter& /*out*/) const {
    return false;
  }
  /** Whether this build has a GPU renderer for `api` (decides what the adapters declare). */
  virtual bool hasGpu(GpuApi) const { return false; }
  virtual std::unique_ptr<GpuRenderer> makeGpuRenderer(GpuApi) const { return nullptr; }
};

/** Defined once by each plugin. */
const Effect& effect();

}  // namespace vv
