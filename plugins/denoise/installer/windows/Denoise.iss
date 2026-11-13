; Denoise (research prototype) — Windows installer for Premiere Pro on the shared template.
#define VV_APP_ID "{{4a3ed3f2-5d66-4e55-bb2c-7c5d07285b7a}"
#define VV_NAME "Denoise (research prototype)"
#define VV_BINARY "Denoise"
#define VV_X64_DIR X64Dir
#ifdef Arm64Dir
  #define VV_ARM64_DIR Arm64Dir
#endif
#include "..\..\..\..\shared\engine\installer\windows\vv_premiere_setup.iss"
