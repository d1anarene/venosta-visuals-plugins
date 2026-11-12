// Loads FilmEffects.ofx the way an OFX host does (dlopen / LoadLibrary), then checks the two
// OpenFX entry points and that Film Effects is exported with the expected identifier.
// Does not render: rendering is covered by the core tests; host behaviour needs DaVinci Resolve.
#include <cstdio>
#include <cstring>
#include <string>

#include "ofxCore.h"
#include "ofxImageEffect.h"

#ifdef _WIN32
#include <windows.h>
static void* openLib(const char* p) { return reinterpret_cast<void*>(LoadLibraryA(p)); }
static void* sym(void* lib, const char* n) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), n)); }
#else
#include <dlfcn.h>
static void* openLib(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* sym(void* lib, const char* n) { return dlsym(lib, n); }
#endif

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: ofx_load_test <FilmEffects.ofx>\n"); return 2; }
  void* lib = openLib(argv[1]);
  if (!lib) {
#ifndef _WIN32
    std::fprintf(stderr, "cannot load %s: %s\n", argv[1], dlerror());
#else
    std::fprintf(stderr, "cannot load %s\n", argv[1]);
#endif
    return 1;
  }
  using CountFn = int (*)();
  using GetFn = OfxPlugin* (*)(int);
  auto count = reinterpret_cast<CountFn>(sym(lib, "OfxGetNumberOfPlugins"));
  auto get = reinterpret_cast<GetFn>(sym(lib, "OfxGetPlugin"));
  if (!count || !get) { std::fprintf(stderr, "OFX entry points missing\n"); return 1; }
  const char* expected[] = {"com.venostavisuals.FilmEffects"};
  const int n = count();
  if (n != 1) { std::fprintf(stderr, "expected 1 plug-in, got %d\n", n); return 1; }
  int failures = 0;
  for (int i = 0; i < n; ++i) {
    OfxPlugin* p = get(i);
    if (!p || !p->pluginIdentifier || !p->mainEntry || !p->setHost) { std::fprintf(stderr, "plug-in %d incomplete\n", i); ++failures; continue; }
    const bool ok = std::strcmp(p->pluginApi, kOfxImageEffectPluginApi) == 0 && p->apiVersion == 1 &&
                    std::strcmp(p->pluginIdentifier, expected[i]) == 0 && p->pluginVersionMajor == 1;
    std::printf("%s %s v%u.%u (%s v%d)\n", ok ? "ok  " : "FAIL", p->pluginIdentifier, p->pluginVersionMajor,
                p->pluginVersionMinor, p->pluginApi, p->apiVersion);
    if (!ok) ++failures;
  }
  return failures ? 1 : 0;
}
