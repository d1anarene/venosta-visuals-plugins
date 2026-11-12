// Denoise — orthonormal opponent colour transform (luma + two chroma axes). Orthonormal, so white
// noise of variance s^2 in RGB stays white with variance s^2 in every opponent channel, and the
// inverse is the transpose. Shared verbatim by CPU, CUDA and Metal.
#ifndef DN_COLOR_H
#define DN_COLOR_H
#include "vv/kernels.h"

#define DN_INV_SQRT3 0.57735026919f
#define DN_INV_SQRT2 0.70710678119f
#define DN_INV_SQRT6 0.40824829046f

// o.x: luma (R+G+B)/sqrt3, o.y: (R-B)/sqrt2, o.z: (R-2G+B)/sqrt6.
VV_FN vv_float3 dn_to_opponent(vv_float3 c) {
  return vv_make3((c.x + c.y + c.z) * DN_INV_SQRT3, (c.x - c.z) * DN_INV_SQRT2, (c.x - 2.0f * c.y + c.z) * DN_INV_SQRT6);
}
VV_FN vv_float3 dn_from_opponent(vv_float3 o) {
  return vv_make3(o.x * DN_INV_SQRT3 + o.y * DN_INV_SQRT2 + o.z * DN_INV_SQRT6,
                  o.x * DN_INV_SQRT3 - 2.0f * o.z * DN_INV_SQRT6,
                  o.x * DN_INV_SQRT3 - o.y * DN_INV_SQRT2 + o.z * DN_INV_SQRT6);
}

#endif  // DN_COLOR_H
