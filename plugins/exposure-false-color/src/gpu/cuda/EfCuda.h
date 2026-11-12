// Exposure & False Color — CUDA pass (NVIDIA). Default stream, finished on return.
#pragma once
#include <cstdint>

#include "ef.h"

namespace ef {
/** One frame on the current CUDA device; src/dst device pointers (may be the same). */
bool cudaRender(const void* src, void* dst, bool half, uint32_t width, uint32_t height, uint32_t rowPixels,
                const Params& params, bool rgba, bool bottomUp);
}  // namespace ef
