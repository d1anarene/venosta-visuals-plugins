// Film Effects — native effect for Premiere Pro (After Effects effect API + Premiere extensions).
//
// STATUS: written against the documented After Effects / Premiere Pro SDK API, NOT YET COMPILED
// (the Adobe SDKs are not available in the environment this was written in). See
// docs/FEASIBILITY.md and PROGRESS.md.
#pragma once

#define FE_MAJOR_VERSION 1
#define FE_MINOR_VERSION 0
#define FE_BUG_VERSION 0
#define FE_STAGE_VERSION PF_Stage_DEVELOP
#define FE_BUILD_VERSION 1

// Must equal AE_Effect_Match_Name in FilmEffectsPiPL.r. NEVER change it after release:
// projects reference the effect by this name.
#define FE_MATCH_NAME "VVIS FilmEffects"
#define FE_NAME "Film Effects"
#define FE_CATEGORY "Venosta Visuals"

// Parameter indices (order in Effect Controls). Group start/end entries count as parameters.
enum {
  FE_INPUT = 0,
  FE_PRESET,
  FE_MASTER_ENABLE,
  FE_MIX,
  FE_WORKING_SPACE,
  FE_QUALITY,
  FE_BLOOM_GROUP,
  FE_BLOOM_ENABLE,
  FE_BLOOM_INTENSITY,
  FE_BLOOM_THRESHOLD,
  FE_BLOOM_KNEE,
  FE_BLOOM_RADIUS,
  FE_BLOOM_GROUP_END,
  FE_HAL_GROUP,
  FE_HAL_ENABLE,
  FE_HAL_INTENSITY,
  FE_HAL_THRESHOLD,
  FE_HAL_RADIUS,
  FE_HAL_WARMTH,
  FE_HAL_GROUP_END,
  FE_GRAIN_GROUP,
  FE_GRAIN_ENABLE,
  FE_GRAIN_AMOUNT,
  FE_GRAIN_SIZE,
  FE_GRAIN_ROUGHNESS,
  FE_GRAIN_SHADOWS,
  FE_GRAIN_MIDTONES,
  FE_GRAIN_HIGHLIGHTS,
  FE_GRAIN_COLOR,
  FE_GRAIN_CHROMA,
  FE_GRAIN_SEED,
  FE_GRAIN_ANIMATE,
  FE_GRAIN_GROUP_END,
  FE_NUM_PARAMS
};

// Stable on-disk IDs (saved in projects). Append only; never renumber.
enum {
  ID_PRESET = 1,
  ID_MASTER_ENABLE,
  ID_MIX,
  ID_WORKING_SPACE,
  ID_QUALITY,
  ID_BLOOM_GROUP,
  ID_BLOOM_ENABLE,
  ID_BLOOM_INTENSITY,
  ID_BLOOM_THRESHOLD,
  ID_BLOOM_KNEE,
  ID_BLOOM_RADIUS,
  ID_BLOOM_GROUP_END,
  ID_HAL_GROUP,
  ID_HAL_ENABLE,
  ID_HAL_INTENSITY,
  ID_HAL_THRESHOLD,
  ID_HAL_RADIUS,
  ID_HAL_WARMTH,
  ID_HAL_GROUP_END,
  ID_GRAIN_GROUP,
  ID_GRAIN_ENABLE,
  ID_GRAIN_AMOUNT,
  ID_GRAIN_SIZE,
  ID_GRAIN_ROUGHNESS,
  ID_GRAIN_SHADOWS,
  ID_GRAIN_MIDTONES,
  ID_GRAIN_HIGHLIGHTS,
  ID_GRAIN_COLOR,
  ID_GRAIN_CHROMA,
  ID_GRAIN_SEED,
  ID_GRAIN_ANIMATE,
  ID_GRAIN_GROUP_END,
};

// Popup item lists ('|' separated, 1-based values in the host).
#define FE_PRESET_ITEMS \
  "(choose to apply)|Subtle Film|Soft Highlight Glow|Night Lights|Fine 35mm Grain|Coarse 16mm Grain|Halation Only|Off"
#define FE_SPACE_ITEMS "Rec.709 / Gamma 2.4 (SDR)|sRGB|Linear"
#define FE_QUALITY_ITEMS "Normal|High"
#define FE_GRAIN_COLOR_ITEMS "Luma (neutral)|Color"
