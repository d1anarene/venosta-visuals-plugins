// Loads each Film Effects .aex the way Premiere does (LoadLibrary) and checks the two entry points
// of an After Effects-API effect. Run on the machine architecture the plug-in was built for
// (x64, or ARM64 on a Windows-on-Arm machine). Does not render: that needs Premiere.
// Usage: aex_load_test <plugin.aex> [...]
#include <windows.h>

#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: aex_load_test <plugin.aex> [...]\n");
    return 2;
  }
  int failures = 0;
  for (int i = 1; i < argc; ++i) {
    HMODULE lib = LoadLibraryA(argv[i]);
    if (!lib) {
      std::printf("FAIL %s: LoadLibrary error %lu\n", argv[i], GetLastError());
      ++failures;
      continue;
    }
    const bool ok = GetProcAddress(lib, "EffectMain") && GetProcAddress(lib, "PluginDataEntryFunction2");
    std::printf("%s %s\n", ok ? "ok  " : "FAIL (entry points missing)", argv[i]);
    if (!ok) ++failures;
    FreeLibrary(lib);
  }
  return failures ? 1 : 0;
}
