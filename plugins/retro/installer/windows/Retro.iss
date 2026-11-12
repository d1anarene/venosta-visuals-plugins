; Retro — Windows installer for Premiere Pro on the shared template.
#define VV_APP_ID "{{48b25efd-af9f-41ef-8a3c-d81d31eae2c1}"
#define VV_NAME "Retro"
#define VV_BINARY "Retro"
#define VV_X64_DIR X64Dir
#ifdef Arm64Dir
  #define VV_ARM64_DIR Arm64Dir
#endif
#include "..\..\..\..\shared\engine\installer\windows\vv_premiere_setup.iss"
