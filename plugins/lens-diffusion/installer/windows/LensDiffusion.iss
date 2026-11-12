; Lens & Diffusion — Windows installer for Premiere Pro, on the shared template
; (shared/engine/installer/windows/vv_premiere_setup.iss). Built in CI (vv-premiere.yml):
;   ISCC /DAppVersion=1.0.x /DX64Dir=<x64 folder> /DArm64Dir=<ARM64 folder> /DRepoRoot=<repo> /DOutputDir=<out> LensDiffusion.iss

#define VV_APP_ID "{{224a89e1-7a85-4d84-a80b-bed085b13707}"
#define VV_NAME "Lens & Diffusion"
#define VV_BINARY "LensDiffusion"
#define VV_X64_DIR X64Dir
#ifdef Arm64Dir
  #define VV_ARM64_DIR Arm64Dir
#endif

#include "..\..\..\..\shared\engine\installer\windows\vv_premiere_setup.iss"
