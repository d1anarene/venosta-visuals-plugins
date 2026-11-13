// Research probe PiPL; wide-time-input flags must agree with premiere_cpu_probe.cpp.
// A plugin's own .r file defines VV_PIPL_NAME, VV_PIPL_MATCH and VV_PIPL_CATEGORY (identical to
// its vv::EffectInfo) and then #includes this file. Version and flags must agree with GlobalSetup()
// in vv_premiere_effect.cpp. Compiled with Rez (macOS) or cl + PiPLtool (Windows); see
// cmake/vv_premiere.cmake.
#ifndef VV_PIPL_OUTFLAGS
  #define VV_PIPL_OUTFLAGS 0x02000002
#endif
#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
  #include <AE_General.r>
#endif

resource 'PiPL' (16000) {
  {
    Kind { AEEffect },
    Name { VV_PIPL_NAME },
    Category { VV_PIPL_CATEGORY },
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
    /* Default DEEP_COLOR_AWARE | WIDE_TIME_INPUT (CPU/Metal probes); a probe's .r may define
       VV_PIPL_OUTFLAGS (CUDA probe: 0x02000000, no WIDE_TIME_INPUT). Must match its GlobalSetup. */
    AE_Effect_Global_OutFlags { VV_PIPL_OUTFLAGS },
    /* PF_OutFlag2_FLOAT_COLOR_AWARE (1 << 12) | PF_OutFlag2_SUPPORTS_THREADED_RENDERING (1 << 27) */
    AE_Effect_Global_OutFlags_2 { 0x08001000 },
    AE_Effect_Match_Name { VV_PIPL_MATCH },
    AE_Reserved_Info { 0 },
    AE_Effect_Support_URL { "https://venostavisuals.com" }
  }
};
