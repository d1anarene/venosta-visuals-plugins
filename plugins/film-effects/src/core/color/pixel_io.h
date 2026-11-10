// Conversion between host pixel buffers and the core's working image.
// Strides are in BYTES and may be negative (bottom-up buffers).
#pragma once
#include <cstddef>
#include <cstdint>

#include "../image.h"

namespace fe {

enum class PixelLayout {
  BGRA_8u,      // Premiere native 8-bit, straight alpha
  BGRA_32f,     // Premiere native float, straight alpha (gamma-encoded working space)
  ARGB_8u,      // After Effects 8-bit, premultiplied alpha
  ARGB_16u,     // After Effects 16-bit (0..32768), premultiplied alpha
  ARGB_32f,     // After Effects 32-bit float, premultiplied alpha
  RGBA_32f,     // CLI / tests, straight alpha
};

/** Read a host buffer into a straight-alpha RGBA float image. */
void readPixels(const void* base, ptrdiff_t strideBytes, int width, int height, PixelLayout layout,
                Image& out);

/** Write a straight-alpha RGBA float image back to a host buffer. Alpha is copied unchanged from
 *  `alphaSource` when given (guarantees bit-exact alpha), else taken from the image. */
void writePixels(const Image& img, void* base, ptrdiff_t strideBytes, PixelLayout layout,
                 const void* alphaSourceBase = nullptr, ptrdiff_t alphaSourceStride = 0);

}  // namespace fe
