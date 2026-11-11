// Film Effects — Metal backend. Host-API independent: takes Metal objects and core FilmParams, so
// it can be driven by Premiere's GPU entry point or by a standalone test harness on macOS.
// STATUS: NOT YET COMPILED.
#pragma once
#ifdef __OBJC__
#import <Metal/Metal.h>
#endif
#include <cstdint>
#include <memory>
#include <string>

#include "params/params.h"

namespace fe {

struct MetalFrame {
#ifdef __OBJC__
  id<MTLBuffer> buffer = nil;  // BGRA, float4 or half4 per pixel
#else
  void* buffer = nullptr;
#endif
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rowPixels = 0;     // row stride in pixels
  bool halfFloat = false;     // true for 16f GPU frames
};

class MetalFilmEngine {
 public:
  /** `metallibPath`: compiled FilmEffects.metallib (shipped inside the plugin bundle). */
#ifdef __OBJC__
  MetalFilmEngine(id<MTLDevice> device, const std::string& metallibPath);
#endif
  ~MetalFilmEngine();
  bool ok() const;
  const std::string& error() const;

  /** Encode the whole effect. `src` and `dst` may be the same buffer (in-place). Commands are
   *  encoded into `commandBuffer`; the caller commits (and waits if needed). */
#ifdef __OBJC__
  bool encode(id<MTLCommandBuffer> commandBuffer, const MetalFrame& src, const MetalFrame& dst,
              const FilmParams& params, int64_t frameKey);
#endif

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fe
