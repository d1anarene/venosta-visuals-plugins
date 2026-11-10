// PiPL resource for Film Effects. Values must agree with GlobalSetup() in FilmEffects.cpp.
// STATUS: not yet compiled (needs the After Effects SDK: AE_General.r, AE_EffectVers.h).
#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
  #include <AE_General.r>
#endif

resource 'PiPL' (16000) {
  {
    Kind { AEEffect },
    Name { "Film Effects" },
    Category { "Venosta Visuals" },
#ifdef AE_OS_WIN
  #if defined(AE_PROC_INTELx64)
    CodeWin64X86 { "EffectMain" },
  #elif defined(AE_PROC_ARM64)
    CodeWinARM64 { "EffectMain" },
  #endif
#elif defined(AE_OS_MAC)
    CodeMacIntel64 { "EffectMain" },
    CodeMacARM64 { "EffectMain" },
#endif
    AE_PiPL_Version { 2, 0 },
    AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
    /* PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1) = (1 << 19) | 1 */
    AE_Effect_Version { 524289 },
    AE_Effect_Info_Flags { 0 },
    /* PF_OutFlag_DEEP_COLOR_AWARE (1 << 25) */
    AE_Effect_Global_OutFlags { 0x02000000 },
    /* PF_OutFlag2_FLOAT_COLOR_AWARE (1 << 12) | PF_OutFlag2_SUPPORTS_THREADED_RENDERING (1 << 27) */
    AE_Effect_Global_OutFlags_2 { 0x08001000 },
    AE_Effect_Match_Name { "VVIS FilmEffects" },
    AE_Reserved_Info { 0 },
    AE_Effect_Support_URL { "https://venostavisuals.com" }
#ifdef FE_PIPL_SEARCH_KEYWORDS
    /* Premiere Pro 27.0+ (beta) Effects panel search. Needs an SDK whose AE_General.r defines
       these properties; enable with -DFE_PIPL_SEARCH_KEYWORDS. */
    ,
    AE_Effect_Search_Keywords {
      "{\"en_US\": [\"film\", \"bloom\", \"halation\", \"grain\", \"glow\", \"diffusion\", \"texture\"]}"
    },
    AE_Effect_Description { "Bloom, halation and film grain for finishing after Lumetri." }
#endif
  }
};
