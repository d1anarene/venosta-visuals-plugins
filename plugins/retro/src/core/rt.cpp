#include "rt.h"

#include <algorithm>
#include <cmath>

#include "vv/blur.h"
#include "vv/parallel.h"

namespace rt {
namespace {
float clampf(float v, float lo, float hi, float fallback) {
  return std::isfinite(v) ? std::min(std::max(v, lo), hi) : fallback;
}

// Bilinear sample of channel c of a 3-channel plane at (sx, sy), clamped to the edges. The GPU
// passes use the same formula (src/gpu/common/rt_gpu_pixels.h).
float sample(const vv::Plane& p, float sx, float sy, int c) {
  const float fx = std::clamp(sx, 0.f, float(p.width - 1)), fy = std::clamp(sy, 0.f, float(p.height - 1));
  const int x0 = int(fx), y0 = int(fy), x1 = std::min(x0 + 1, p.width - 1), y1 = std::min(y0 + 1, p.height - 1);
  const float tx = fx - float(x0), ty = fy - float(y0);
  const float a = p.at(x0, y0)[c], b = p.at(x1, y0)[c], d = p.at(x0, y1)[c], e = p.at(x1, y1)[c];
  const float top = a + (b - a) * tx, bot = d + (e - d) * tx;
  return top + (bot - top) * ty;
}
}  // namespace

Params sanitized(const Params& in) {
  const Params d;
  Params p = in;
  p.mix = clampf(p.mix, 0.f, 1.f, d.mix);
  p.lumaSoft = clampf(p.lumaSoft, 0.f, 6.f, d.lumaSoft);
  p.ringing = clampf(p.ringing, 0.f, 1.f, d.ringing);
  p.chromaBlur = clampf(p.chromaBlur, 0.f, 30.f, d.chromaBlur);
  p.chromaShift = clampf(p.chromaShift, -10.f, 10.f, d.chromaShift);
  p.saturation = clampf(p.saturation, 0.f, 2.f, d.saturation);
  p.jitter = clampf(p.jitter, 0.f, 10.f, d.jitter);
  p.tracking = clampf(p.tracking, 0.f, 1.f, d.tracking);
  p.dropouts = clampf(p.dropouts, 0.f, 1.f, d.dropouts);
  p.headSwitch = clampf(p.headSwitch, 0.f, 1.f, d.headSwitch);
  p.lumaNoise = clampf(p.lumaNoise, 0.f, 1.f, d.lumaNoise);
  p.chromaNoise = clampf(p.chromaNoise, 0.f, 1.f, d.chromaNoise);
  p.scanlines = clampf(p.scanlines, 0.f, 1.f, d.scanlines);
  p.lineCount = clampf(p.lineCount, 60.f, 1080.f, d.lineCount);
  if (p.maskType < 0 || p.maskType > 2) p.maskType = d.maskType;
  p.maskStrength = clampf(p.maskStrength, 0.f, 1.f, d.maskStrength);
  p.curvature = clampf(p.curvature, 0.f, 1.f, d.curvature);
  return p;
}

Blurs blurs(const Params& p, int height) {
  const float s = float(height) / 1080.f;
  if (!p.bleed) return {0.f, 0.f, 0.f};
  return {p.lumaSoft * s, p.ringing > 0.f ? std::max(3.f * p.lumaSoft, 2.f) * s : 0.f, p.chromaBlur * s};
}

RtInfo info(const Params& p, int w, int h, int64_t frameKey) {
  RtInfo i{};
  i.width = unsigned(w);
  i.height = unsigned(h);
  const uint64_t t = p.animate ? uint64_t(frameKey) : 0u;
  i.key = vv_hash3(vv_uint(p.seed), vv_uint(t & 0xffffffffu), vv_uint(t >> 32));
  i.bleed = p.bleed;
  i.tape = p.tape;
  i.noise = p.noise;
  i.crt = p.crt;
  i.maskType = p.maskType;
  i.ringing = p.ringing;
  i.chromaShiftPx = p.bleed ? p.chromaShift * float(h) / 1080.f : 0.f;
  i.saturation = p.saturation;
  i.jitterPx = p.jitter;
  i.tracking = p.tracking;
  i.dropouts = p.dropouts;
  i.headSwitch = p.headSwitch;
  i.lumaNoise = p.lumaNoise;
  i.chromaNoise = p.chromaNoise;
  i.scanlines = p.scanlines;
  i.lineCount = p.lineCount;
  i.maskStrength = p.maskStrength;
  i.curvature = p.curvature;
  // Tracking band rolls slowly through the frame (and a little beyond, so it also disappears).
  const double phase = double(t % 100000u) * 0.0137 + vv_unit(vv_hash(vv_uint(p.seed) + 77u));
  i.trackingPos = float((phase - std::floor(phase)) * 1.3 * h - 0.15 * h);
  i.scale = float(h) / 1080.f;
  i.mix = p.mix;
  return i;
}

bool process(Image& img, const Params& raw, int64_t frameKey) {
  if (img.empty()) return false;
  const Params p = sanitized(raw);
  if (!p.anyActive()) return true;
  const int w = img.width, h = img.height;
  vv::Plane yuv(w, h, 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float* s = img.at(x, y);
      const vv_float3 c = rt_rgb_to_yuv(vv_make3(s[0], s[1], s[2]));
      float* d = yuv.at(x, y);
      d[0] = c.x; d[1] = c.y; d[2] = c.z;
    }
  const Blurs b = blurs(p, h);
  auto blurred = [&](float sigma) { return sigma >= 0.05f ? vv::gaussianBlurHorizontal(yuv, sigma) : yuv; };
  const vv::Plane luma = blurred(b.luma), ring = blurred(b.ring), chroma = blurred(b.chroma);
  const RtInfo inf = info(p, w, h, frameKey);
  Image out = img;
  vv::parallelRows(h, [&](int y) {
    for (int x = 0; x < w; ++x) {
      float* px = out.at(x, y);
      if (px[3] <= 0.f) continue;  // never invent colour where the layer is fully transparent
      const vv_float3 g = rt_geometry(float(x), float(y), inf);
      const float ya = sample(luma, g.x, g.y, 0), yr = sample(ring, g.x, g.y, 0);
      const float u = sample(chroma, g.x + inf.chromaShiftPx, g.y, 1), v = sample(chroma, g.x + inf.chromaShiftPx, g.y, 2);
      const vv_float3 o = vv_make3(px[0], px[1], px[2]);
      const vv_float3 c = rt_finish(ya, yr, u, v, o, float(x), float(y), g.x, g.y, g.z, inf);
      const float cc[3] = {c.x, c.y, c.z};
      for (int k = 0; k < 3; ++k)
        if (std::isfinite(cc[k])) px[k] = cc[k];
    }
  });
  img = std::move(out);
  return true;
}

const std::vector<Preset>& builtinPresets() {
  static const std::vector<Preset> v = [] {
    std::vector<Preset> r;
    auto add = [&](const char* n, auto&& fn) {
      Params p;
      fn(p);
      r.push_back({n, p});
    };
    add("Home Video Tape", [](Params&) {});
    add("Worn Tape", [](Params& p) {
      p.chromaBlur = 12.f; p.chromaShift = 3.5f; p.saturation = 0.75f; p.jitter = 3.f; p.tracking = 0.6f;
      p.dropouts = 0.6f; p.headSwitch = 0.9f; p.lumaNoise = 0.3f; p.chromaNoise = 0.25f;
    });
    add("Broadcast 80s", [](Params& p) {
      p.lumaSoft = 0.8f; p.ringing = 0.2f; p.chromaBlur = 5.f; p.chromaShift = 1.f; p.saturation = 1.05f;
      p.tape = false; p.lumaNoise = 0.08f; p.chromaNoise = 0.05f;
    });
    add("CRT Monitor", [](Params& p) {
      p.tape = false; p.noise = false; p.lumaSoft = 0.6f; p.ringing = 0.1f; p.chromaBlur = 2.f; p.chromaShift = 0.f;
      p.saturation = 1.f; p.crt = true;
    });
    add("Subtle Analog", [](Params& p) {
      p.lumaSoft = 0.6f; p.ringing = 0.15f; p.chromaBlur = 4.f; p.chromaShift = 1.f; p.saturation = 0.95f;
      p.jitter = 0.5f; p.tracking = 0.f; p.dropouts = 0.f; p.headSwitch = 0.25f; p.lumaNoise = 0.06f; p.chromaNoise = 0.04f;
    });
    add("Off", [](Params& p) { p.bleed = p.tape = p.noise = p.crt = false; });
    return r;
  }();
  return v;
}

const Preset* findPreset(const std::string& name) {
  for (const auto& p : builtinPresets())
    if (p.name == name) return &p;
  return nullptr;
}

}  // namespace rt
