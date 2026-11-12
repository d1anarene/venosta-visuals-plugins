; Film Effects for DaVinci Resolve (OpenFX) — Windows installer on the shared template
; (shared/engine/installer/windows/vv_ofx_setup.iss). Built in CI:
;   ISCC /DAppVersion=1.0.x /DBundleDir=<FilmEffects.ofx.bundle> /DRepoRoot=<repo> /DOutputDir=<out> FilmEffectsResolve.iss
; Contents\Win64: x64 with CPU and NVIDIA CUDA paths; Contents\Win-arm64ec: Windows on Arm (CPU).

#define VV_APP_ID "{{5d0c1f4e-7a7b-4f43-9a55-2f3cbb8c6e21}"
#define VV_NAME "Film Effects"
#define VV_BUNDLE "FilmEffects"

#include "..\..\..\..\shared\engine\installer\windows\vv_ofx_setup.iss"
