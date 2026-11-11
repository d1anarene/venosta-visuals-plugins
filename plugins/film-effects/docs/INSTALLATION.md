# Building and installing (macOS)

> The plugin has **not yet been built or tested on a Mac**. These are the intended steps; please
> report problems.

## 1. Get the SDKs
Download from the Adobe Developer Console (free Adobe account): **After Effects Plug-in SDK** and
**Premiere Pro C++ SDK** (same year as your Premiere). Unzip both.

## 2. Build
Requirements: macOS 13+, Xcode 15+ (Metal toolchain), CMake 3.20+.
```bash
cd plugins/film-effects
./scripts/build-macos.sh "/path/to/AfterEffectsSDK/Examples" "/path/to/Premiere Pro SDK"
```
This runs the core tests, compiles the PiPL with `Rez`, the Metal kernels into
`FilmEffects.metallib`, and produces `build-mac/.../FilmEffects.plugin` (universal arm64 + x86_64).

**Build from the SDK sample (fallback)**: if the CMake build cannot find SDK headers, open the
`SDK_ProcAmp` Xcode project from the Premiere SDK, replace its sources with
`src/hosts/premiere/*` + `src/gpu/metal/*` + `src/core/**/*.cpp`, add `src/core` to the header
search paths, replace its `.r` with `FilmEffectsPiPL.r`, and add the `.metal` file.

## 3. Install
```bash
./scripts/install-dev-macos.sh
```
Copies `FilmEffects.plugin` to `/Library/Application Support/Adobe/Common/Plugins/7.0/MediaCore/`
(admin password required; an existing copy is moved to a timestamped backup first).
Restart Premiere Pro. The effect is in **Effects → Video Effects → Venosta Visuals → Film Effects**.

Unsigned plugins may be blocked by Gatekeeper on first load; sign with your Developer ID or allow it
in System Settings → Privacy & Security.

## Uninstall / update
Quit Premiere, delete `FilmEffects.plugin` from the folder above (and any `.backup-*` copies you no
longer need), install the new build, restart Premiere. Projects keep their settings because the
effect's match name (`VVIS FilmEffects`) and parameter IDs never change.
