// Retro — per-pixel maths shared verbatim by the CPU reference, CUDA and Metal (C++17, CUDA C++,
// MSL). Buffers are sampled by each backend; everything else happens here:
//   rt_geometry(): where the output pixel samples the (horizontally blurred) Y'UV signal
//                  (CRT curvature, tape line jitter, tracking band, head switching);
//   rt_finish():   Y'UV samples -> encoded RGB (ringing, chroma, noise, dropouts, CRT scanlines,
//                  mask, curved-screen edge).
// The signal is processed in the encoded (gamma) domain like real video, in BT.601 Y'UV.
#ifndef RT_KERNELS_H
#define RT_KERNELS_H

#include "vv/kernels.h"

// All members 4 bytes: identical layout in C++, CUDA and Metal.
struct RtInfo {
  unsigned int width, height;
  unsigned int key;        // per-frame key (seed + frame)
  int bleed, tape, noise, crt, maskType;  // maskType: 0 none, 1 aperture grille, 2 shadow mask
  float ringing, chromaShiftPx, saturation;
  float jitterPx, tracking, dropouts, headSwitch;
  float lumaNoise, chromaNoise;
  float scanlines, lineCount, maskStrength, curvature;
  float trackingPos;       // centre of the tracking band, rows from the top
  float scale;             // frame height / 1080
  float mix;
};

VV_FN vv_float3 rt_rgb_to_yuv(vv_float3 c) {
  float y = 0.299f * c.x + 0.587f * c.y + 0.114f * c.z;
  return vv_make3(y, 0.492f * (c.z - y), 0.877f * (c.x - y));
}
VV_FN vv_float3 rt_yuv_to_rgb(vv_float3 c) {
  float r = c.x + c.z / 0.877f, b = c.x + c.y / 0.492f;
  float g = (c.x - 0.299f * r - 0.114f * b) / 0.587f;
  return vv_make3(r, g, b);
}

VV_FN float rt_signed_hash(vv_uint h) { return vv_unit(h) * 2.0f - 1.0f; }

// Sampling position for output pixel (x, ty) (top row = 0). Returns (sx, sy, inside):
// sx/sy in pixels of the source (sy counted from the top), inside 0..1 (curved-screen edge).
VV_FN vv_float3 rt_geometry(float x, float ty, RtInfo i) {
  const float w = float(i.width), h = float(i.height);
  float sx = x, sy = ty, inside = 1.0f;
  if (i.crt != 0 && i.curvature > 0.0f) {
    float u = (x + 0.5f) / w * 2.0f - 1.0f, v = (ty + 0.5f) / h * 2.0f - 1.0f;
    float k = i.curvature * 0.25f, r2 = u * u + v * v;
    u *= 1.0f + k * r2;
    v *= 1.0f + k * r2;
    const float edge = 0.004f + 0.02f * i.curvature;  // soft screen edge
    inside = (1.0f - vv_smoothstep(1.0f - edge, 1.0f, vv_fabs(u))) * (1.0f - vv_smoothstep(1.0f - edge, 1.0f, vv_fabs(v)));
    sx = (u + 1.0f) * 0.5f * w - 0.5f;
    sy = (v + 1.0f) * 0.5f * h - 0.5f;
  }
  if (i.tape != 0) {
    const float row = vv_floor(vv_clamp(sy + 0.5f, 0.0f, h - 1.0f));
    // Line jitter: smooth over a few dozen lines plus a little per-line randomness.
    float off = i.jitterPx * i.scale *
                (0.75f * vv_value_noise(row / (40.0f * i.scale) + 0.5f, 0.5f, i.key ^ 0x1u) / 1.4f +
                 0.25f * rt_signed_hash(vv_hash3((vv_uint)row, i.key, 0x2u)));
    // Tracking band: a band of heavily disturbed lines rolling through the frame.
    const float bandH = 0.06f * h;
    const float d = vv_fabs(row - i.trackingPos) / bandH;
    if (i.tracking > 0.0f && d < 1.0f) {
      const float s = (1.0f - d * d) * i.tracking;
      off += s * 30.0f * i.scale * rt_signed_hash(vv_hash3((vv_uint)row, i.key, 0x3u));
    }
    // Head switching: the bottom ~1.5 % of the lines bend to one side.
    const float hsH = vv_max(3.0f, 0.015f * h);
    if (i.headSwitch > 0.0f && row > h - hsH) {
      const float t = (row - (h - hsH)) / hsH;
      off += i.headSwitch * 25.0f * i.scale * t * t * (0.8f + 0.2f * rt_signed_hash(vv_hash3((vv_uint)row, i.key, 0x4u)));
    }
    sx += off;
  }
  return vv_make3(sx, sy, inside);
}

// Final colour. ya: luma (soft), yr: luma blurred wider (for ringing), uv: chroma (blurred, shifted
// sample), all from the Y'UV signal; orig: the untouched encoded RGB (for mix); (x, ty, sx, sy):
// output pixel and its sampling position.
VV_FN vv_float3 rt_finish(float ya, float yr, float u, float v, vv_float3 orig, float x, float ty, float sx, float sy,
                          float inside, RtInfo i) {
  const float w = float(i.width), h = float(i.height);
  float y = ya;
  if (i.bleed != 0) {
    y = ya + i.ringing * (ya - yr);  // edge enhancement overshoot ("ringing")
    u *= i.saturation;
    v *= i.saturation;
  }
  const float row = vv_floor(vv_clamp(sy + 0.5f, 0.0f, h - 1.0f));
  if (i.tape != 0) {
    // Tracking band also brings noise and a little lift.
    const float d = vv_fabs(row - i.trackingPos) / (0.06f * h);
    if (i.tracking > 0.0f && d < 1.0f) {
      const float s = (1.0f - d * d) * i.tracking;
      y += s * (0.08f + 0.35f * vv_max(rt_signed_hash(vv_hash3((vv_uint)(x / vv_max(i.scale, 1.0f)), (vv_uint)row, i.key ^ 0x5u)), 0.0f));
    }
    // Dropouts: short bright streaks on random lines.
    if (i.dropouts > 0.0f) {
      const vv_uint hr = vv_hash3((vv_uint)row, i.key, 0x6u);
      if (vv_unit(hr) < 0.012f * i.dropouts) {
        const float start = vv_unit(vv_hash(hr + 1u)) * w, len = (20.0f + 120.0f * vv_unit(vv_hash(hr + 2u))) * i.scale;
        const float t = (sx - start) / len;
        if (t >= 0.0f && t <= 1.0f) y = vv_lerp(y, 1.0f, 0.85f * (1.0f - vv_fabs(t * 2.0f - 1.0f)));
      }
    }
  }
  if (i.noise != 0) {
    const vv_uint px = (vv_uint)x, py = (vv_uint)ty;
    y += i.lumaNoise * 0.12f * rt_signed_hash(vv_hash3(px, py, i.key ^ 0x7u));
    // Chroma noise is horizontally smeared, like real colour noise on tape.
    const float cx = x / (4.0f * vv_max(i.scale, 0.25f));
    u += i.chromaNoise * 0.10f * vv_value_noise(cx, ty, i.key ^ 0x8u) / 1.4f;
    v += i.chromaNoise * 0.10f * vv_value_noise(cx, ty, i.key ^ 0x9u) / 1.4f;
  }
  vv_float3 c = rt_yuv_to_rgb(vv_make3(y, u, v));
  if (i.crt != 0) {
    if (i.scanlines > 0.0f) {
      // Scanline profile 0.5 - 0.5 cos(2 pi phase), averaged analytically over the pixel's height,
      // so lines finer than a pixel turn into even darkening instead of moire.
      const float ph0 = sy / h * i.lineCount, dph = i.lineCount / h;
      const float twoPi = 6.2831853f;
      const float dark = 0.5f - 0.5f * (vv_sin(twoPi * (ph0 + dph)) - vv_sin(twoPi * ph0)) / (twoPi * dph);
      c = c * (1.0f - i.scanlines * dark);
    }
    if (i.maskType != 0 && i.maskStrength > 0.0f) {
      const float cell = vv_max(1.0f, i.scale);
      int col = (int)vv_floor(x / cell);
      if (i.maskType == 2) col += 2 * ((int)vv_floor(ty / (2.0f * cell)) & 1);  // shadow mask: offset rows
      col = ((col % 3) + 3) % 3;
      const float m = i.maskStrength, keep = 1.0f - m, comp = 1.0f / (keep + m / 3.0f);
      c = vv_make3(c.x * (keep + (col == 0 ? m : 0.0f)) * comp, c.y * (keep + (col == 1 ? m : 0.0f)) * comp,
                   c.z * (keep + (col == 2 ? m : 0.0f)) * comp);
    }
    c = c * inside;
  }
  if (i.mix < 1.0f) c = orig + (c - orig) * i.mix;
  return c;
}

#endif  // RT_KERNELS_H
