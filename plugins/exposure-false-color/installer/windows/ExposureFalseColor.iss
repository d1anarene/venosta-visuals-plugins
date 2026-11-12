; Exposure & False Color — Windows installer for Premiere Pro on the shared template.
#define VV_APP_ID "{{89de3259-74cc-4901-b18a-19cbbc44a70b}"
#define VV_NAME "Exposure & False Color"
#define VV_BINARY "ExposureFalseColor"
#define VV_X64_DIR X64Dir
#ifdef Arm64Dir
  #define VV_ARM64_DIR Arm64Dir
#endif
#include "..\..\..\..\shared\engine\installer\windows\vv_premiere_setup.iss"
