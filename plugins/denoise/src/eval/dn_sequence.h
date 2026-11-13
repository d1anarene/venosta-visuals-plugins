// Analytic, deterministic sequence fixtures, NOT a calibrated camera/sensor simulator.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vv/image.h"

namespace dn {
namespace eval {

inline constexpr const char* kSequenceEncoding = "ACEScct scalar transfer on linear Rec.709 RGB (curve only; not AP1 ACEScct)";
inline constexpr const char* kSequenceNoiseModel = "Independent scene-linear RGB Gaussian shot/read approximation; no calibrated camera, ISP or compression";
inline constexpr const char* kSequenceGeneratorVersion = "dn-analytic-sequences-v1";

enum class SequenceScenario { StaticDetail, SubpixelPan, MovingOccluder, HardCut, ExposureStep };

struct SequenceConfig {
  int width = 320;
  int height = 180;
  int frameCount = 12;
  uint32_t seed = 7;
  SequenceScenario scenario = SequenceScenario::StaticDetail;
  float exposureStops = -2.f;
  // Independent Gaussian shot/read approximation added in scene-linear RGB AFTER the analytic
  // scene. No Bayer/ISP/compression/fixed pattern; no claim of calibrated log-camera noise.
  float shotNoise = 0.00005f;  // variance per unit linear signal
  float readNoise = 0.0005f;  // linear standard deviation
  float panX = 0.65f;         // background camera movement in actual input pixels per frame
  float panY = 0.25f;
  bool frameMarker = false; // optional decimal frame index strip for disposable host fixtures;
                            // excluded from every evaluation mask, not a benchmark feature
};

struct SequenceFrame {
  vv::Image clean;       // Rec.709 primaries, ACEScct scalar curve; not full AP1 ACEScct colour
  vv::Image noisy;       // same domain/range as clean, no clipping; opaque straight alpha
  vv::Image linearClean;
  // Backward flow (dx,dy): current pixel index -> previous pixel index. Units: input pixels.
  // The flow is analytic geometry, NOT an estimated or practically attainable motion field.
  vv::Plane backwardFlow;
  // 1 only when the complete bilinear previous-frame footprint is in bounds and on same layer.
  // Zero on first frames/cuts. Moving boundaries without valid history are disocclusions.
  vv::Plane visibleMask;
  vv::Plane disocclusionMask;
  vv::Plane detailMask;  // analytic fine-texture interiors, excludes scene/occluder boundaries
  vv::Plane detailRegion; // integer labels within detailMask: 1 skin, 2 fabric, 3 foliage, 4 hair,
                          // 5 moving foreground; 0 outside detailMask. One channel, not binary.
  vv::Plane flatMask;    // deliberately flat coloured patches, separate from smooth skin region
  int frameIndex = 0;
  bool cut = false;
};

std::vector<std::string> sequenceScenarioNames();
const char* sequenceScenarioName(SequenceScenario scenario);
SequenceScenario parseSequenceScenario(const std::string& name);

// Throws std::invalid_argument for malformed configurations and std::out_of_range for index.
SequenceFrame generateSequenceFrame(const SequenceConfig& config, int index);

}  // namespace eval
}  // namespace dn
