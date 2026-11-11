# Building and installing

> Mac: user-tested in Premiere Pro 27 on Apple Silicon; macOS 26+.
> Windows: experimental CPU-only and CUDA downloads; host testing pending. Mac downloads are unsigned and
> not Apple-notarized. Export matching, long timeline stability and HDR workflows are unverified.

| | macOS | Windows |
| --- | --- | --- |
| Plugin files | `Film*.plugin` bundles (arm64 + x86_64) | `Film*.aex` (x64) |
| Rendering | CPU + Metal GPU | Separate CPU-only and CUDA + CPU fallback ZIPs |
| Status | User-tested in Premiere Pro 27 on Apple Silicon | CI-built; host testing pending |

## 1. Get the SDKs
Download from the Adobe Developer Console (free Adobe account): **After Effects Plug-in SDK** and
**Premiere Pro C++ SDK** (same year as your Premiere). Unzip both.

## 2. Build (macOS)
Release requires macOS 26+. Source-build requirements: macOS 13+, Xcode 15+ (Metal toolchain), CMake 3.20+.
```bash
cd plugins/film-effects
./scripts/build-macos.sh "/path/to/AfterEffectsSDK/Examples" "/path/to/Premiere Pro SDK"
```
This runs the core tests, compiles the PiPL with `Rez`, the Metal kernels into
`FilmEffects.metallib`, and produces four bundles (universal arm64 + x86_64) in `build-mac/`:
`FilmEffects.plugin`, `FilmBloom.plugin`, `FilmHalation.plugin`, `FilmGrain.plugin`.

**Build from the SDK sample (fallback)**: if the CMake build cannot find SDK headers, open the
`SDK_ProcAmp` Xcode project from the Premiere SDK, replace its sources with
`src/hosts/premiere/*` + `src/gpu/metal/*` + `src/core/**/*.cpp`, add `src/core` to the header
search paths, replace its `.r` with `FilmEffectsPiPL.r`, and add the `.metal` file. For a standalone effect add
the preprocessor define `FE_VARIANT=1` (Bloom), `2` (Halation) or `3` (Grain) to both the C++ and
the Rez settings.

## 2b. Build (Windows)
Requirements: Windows 10/11 x64, Visual Studio 2022 or newer with "Desktop development with C++", CMake 3.20+,
and the **NVIDIA CUDA Toolkit 12.x** (free, developer.nvidia.com) for GPU acceleration. Without the
toolkit the build still works and the plugins render on the CPU. The CUDA runtime is linked
statically, so users only need a normal NVIDIA driver.
In a **Developer PowerShell for Visual Studio**:
```powershell
cd plugins\film-effects
.\scripts\build-windows.ps1 "C:\AfterEffectsSDK\Examples" "C:\Premiere Pro SDK"
```
This runs the core tests, converts the PiPL with the SDK's `PiPLtool.exe` (the step the SDK sample
projects use) and builds `FilmEffects.aex`, `FilmBloom.aex`, `FilmHalation.aex`, `FilmGrain.aex`.
CMake prints "CUDA GPU path enabled" when it found the CUDA toolkit. In Premiere, check
*File → Project Settings → General → Renderer* is "Mercury Playback Engine GPU Acceleration (CUDA)".

## 3. Install (macOS)

Download `FilmEffects-mac-v*.pkg` from the Releases page. Quit Premiere, double-click the installer, then restart
Premiere and look in **Effects → Video Effects → Venosta Visuals**. The destination is
`/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/` (**with a hyphen**).
Premiere 27.0's actual loading log confirms it scans this folder. The initial installer used
`Common/Plugins`, which was not scanned. The corrected installer leaves those inactive files
alone and installs into the scanned folder. Existing copies in the correct folder are backed
up under `/Library/Application Support/Venosta Visuals/Backups/`.

For a local development build:
```bash
./scripts/install-dev-macos.sh
```
Copies the four bundles to `/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/`
(admin password required; an existing copy is moved to a timestamped backup first). Install only
some with e.g. `./scripts/install-dev-macos.sh FilmEffects FilmGrain`.
Restart Premiere Pro. The effects are in **Effects → Video Effects → Venosta Visuals**.

## 3b. Install (Windows)
**Recommended:** run `FilmEffects-Windows-Setup-<version>.exe` from Releases (Inno Setup wizard,
needs admin rights). It installs the four `.aex` files into Premiere's
shared plug-in folder (registry value `Plug-InsDir`, normally
`C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore` — with a hyphen), pre-selects the CUDA build when
`nvcuda.dll` (NVIDIA driver) is present and the CPU-only build otherwise, and registers an
uninstaller in Settings → Apps. Running a newer Setup upgrades in place. It is not code-signed:
SmartScreen may show "Windows protected your PC" → "More info" → "Run anyway". Source:
`installer/windows/FilmEffects.iss`, built in CI. The installer itself has not yet been run on a
Windows machine.

**Manual alternative:** download and extract **one** of the Windows ZIPs from Releases. Choose CUDA for a compatible
NVIDIA GPU (including GTX 1080 / Pascal), or CPU-only for the software-only build. Both install
the same four filenames; installing a different variant replaces the previous one, with backups.
The CUDA build uses Toolkit 12.9.1, includes native `sm_61` code, and statically links its runtime.
Users need a compatible NVIDIA display driver, not the development toolkit. Select Mercury
Playback Engine GPU Acceleration (CUDA) in Premiere's project settings for the GPU path.
Actual GPU activation/rendering still needs Windows host testing.

For downloaded ZIPs, run `install-windows.ps1` from the extracted folder. For local source builds:

In PowerShell **as administrator**:
```powershell
.\scripts\install-dev-windows.ps1
```
Copies the `.aex` files to `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\` (existing copies
are renamed to a timestamped backup first). Restart Premiere Pro.

Unsigned plugins may be blocked by Gatekeeper on first load; sign with your Developer ID or allow it
in System Settings → Privacy & Security.

## Uninstall / update
Quit Premiere, delete the `Film*.plugin` bundles (macOS) or `Film*.aex` files (Windows) from the
folder above (and any `.backup-*` copies
you no longer need), install the new build, restart Premiere. Projects keep their settings because
the match names (`VVIS FilmEffects`, `VVIS FilmBloom`, `VVIS FilmHalation`, `VVIS FilmGrain`) and
parameter IDs never change.

## Package a Mac installer

On macOS with Xcode, from the repository root:

```bash
bash plugins/film-effects/scripts/package-macos.sh /path/to/bundles /path/to/FilmEffects.pkg 1.0.0
```

The bundle folder must contain all four `.plugin` bundles with their PiPL and Metal resources.
The script restores executable permissions, validates both CPU architectures and PiPL resources,
ad hoc signs each bundle, and creates a non-relocatable package for the confirmed MediaCore path.
The installer requires macOS 26+, asks Premiere to be closed, and backs up installed copies before
updating. Ad hoc signing does not provide Apple Developer ID trust or notarization. The package
identifier is retained from the preview so existing installations can update.
