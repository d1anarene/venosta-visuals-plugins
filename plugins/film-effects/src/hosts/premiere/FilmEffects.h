// Film Effects — native effects for Premiere Pro (After Effects effect API + Premiere extensions).
//
// One source tree builds four effects. Premiere supports only one effect (PiPL) per plugin binary,
// so each variant is its own bundle, selected at compile time with FE_VARIANT:
//   0 Film Effects (combined)   1 Film Bloom   2 Film Halation   3 Film Grain
// All four use the same core (src/core), so a standalone effect renders exactly like the
// combined effect with only that section enabled.
//
// STATUS: written against the documented After Effects / Premiere Pro SDK API, NOT YET COMPILED
// against the Adobe SDK. See docs/FEASIBILITY.md and PROGRESS.md.
#pragma once

#ifndef FE_VARIANT
#define FE_VARIANT 0
#endif

#define FE_MAJOR_VERSION 1
#define FE_MINOR_VERSION 0
#define FE_BUG_VERSION 0
#define FE_STAGE_VERSION PF_Stage_DEVELOP
#define FE_BUILD_VERSION 1

// Names must equal Name / AE_Effect_Match_Name in FilmEffectsPiPL.r. NEVER change a match name
// after release: projects reference effects by it.
#if FE_VARIANT == 0
  #define FE_NAME "Film Effects"
  #define FE_MATCH_NAME "VVIS FilmEffects"
#elif FE_VARIANT == 1
  #define FE_NAME "Film Bloom"
  #define FE_MATCH_NAME "VVIS FilmBloom"
#elif FE_VARIANT == 2
  #define FE_NAME "Film Halation"
  #define FE_MATCH_NAME "VVIS FilmHalation"
#elif FE_VARIANT == 3
  #define FE_NAME "Film Grain"
  #define FE_MATCH_NAME "VVIS FilmGrain"
#else
  #error "FE_VARIANT must be 0..3"
#endif
#define FE_CATEGORY "Venosta Visuals"
