# PROGRESS — Film Effects

_Last update: 2026-11-13_


## Host test: macOS and Windows (2026-11-13, owner)

- v1.0.17, Windows PC with NVIDIA GPU: Premiere Pro 2027 and DaVinci Resolve 21; Apple Silicon Mac:
  Premiere Pro 2027 and DaVinci Resolve 21.2. Reported: everything worked (installers, effect in
  the effects list, rendering, controls, presets).
- Not yet tested: exports; Windows on Arm; Linux. Windows GPU renderer: the project was presumably
  set to CUDA, not confirmed.

## Product description and focus (2026-11-13)

- Owner chose **free film finishing effects** as the product description: Bloom, Halation, Film
  Grain, and optional Film Gate, applied to an existing grade in Premiere Pro or DaVinci Resolve.
- Keep the current feature scope; prioritize image quality, usability, performance, and reliability.
  Film-stock models and negative/print pipelines are outside the current scope.
- Updated the plugin README, root listing, roadmap, release-note template, and agent specification.
  Documentation only; rendering and verification status unchanged.

## Shared engine, phases A + B (2026-11-12)

- Owner chose a shared engine (`shared/engine`) over copying code into new plugins.
- Phase A: image types, threading, blur pyramid, pixel formats and generic per-pixel maths moved to
  `shared/engine/core/vv` (`vv_` prefix); Film Effects keeps its own maths.
- Phase B: Film Effects is now a `vv::Effect` (`src/effect/`): parameter table, CPU render,
  presets through `paramChanged`, Metal/CUDA renderers shared by both hosts. The Premiere adapter
  (AE API + GPU entries + PiPL template) and the OpenFX adapter live in `shared/engine/hosts`.
  The old variant mechanism is gone (only Film Effects is built since the standalone effects were
  retired). Saved-project identifiers unchanged: match name `VVIS FilmEffects`, disk IDs, OpenFX id
  `com.venostavisuals.FilmEffects` and parameter names `fe<diskId>`.
- Verified locally (Linux): all tests pass (21,247 checks + GPU emulation + OFX load) and an
  "Old Projector" frame is byte-identical before/after. Premiere adapter only compiles in CI (SDK).

## Film Gate + one effect (2026-11-12)

- Owner: one Film Effects panel instead of separate effects; standalone Film Bloom / Halation /
  Grain retired (Premiere + Resolve); installers remove the old files (Windows CI tests it).
- **Film Gate** section added (`src/core/gate`, shared per-pixel maths in `fe_kernels.h`): gate
  weave (Catmull-Rom, exact for whole-pixel offsets), flicker, rounded soft gate aperture, dust
  (hashed grid, 3x3 cells) and persistent vertical scratches; dark/light/mixed look; own seed.
  New preset "Old Projector" (appended; menu values stay stable). Presets are applied through one
  shared list (`forEachPresetValue`) in Premiere and Resolve.
- Tests: 11 new core tests (21,228 checks pass locally); GPU emulation compares the gate path
  with the CPU reference in Premiere and OpenFX layouts (exact; breaking the bottom-up row mapping
  fails 8 cases). CUDA kernels and the Metal shader/engine got the same two passes; they are
  compiled and compared on CI (Metal vs CPU runs every preset, including Old Projector).
- Not yet seen in Premiere/Resolve.

## Windows on Arm (2026-11-12)

- User: Premiere supports Windows on Arm since 26.0. Premiere: new CI build `win-arm64` on a
  `windows-11-arm` runner (native ARM64, CPU only); the Premiere Setup installs it when
  `IsArm64`, the x64 CUDA build otherwise. `fe_aex_load_test` loads every `.aex` with LoadLibrary
  on its own architecture; an extra CI job installs the Setup on an Arm machine and checks that
  the ARM64 files were installed. The CI step "Show Windows-on-Arm hints in the Adobe SDKs" prints
  whether the SDK samples target ARM64 or ARM64EC. Result (CI run 43190167072): AE SDK 69
  project files with `Release|ARM64`, Premiere 27.0 SDK 7, **none with ARM64EC** — native ARM64 is
  Adobe's choice too.
- CI run 43190167072 / 43190167237: all four `.aex` load on the Arm runner; Premiere Setup installs
  the ARM64 files there and the x64 CUDA files on x64; Resolve Setup installs both architecture
  folders, and the installed Arm64EC / x64 binaries load on their machines.
- Resolve: OpenFX 1.5 packaging says Resolve on Windows on Arm is an Arm64EC host and defines
  `Contents/Win-arm64ec`. New CI build with `-A ARM64EC` on an Arm runner; the Resolve Setup ships
  one bundle with `Win64` (x64 + CUDA) and `Win-arm64ec`; tested by installing on x64 and Arm
  machines and loading the installed binary on each.
- **No Premiere or Resolve test on a Windows-on-Arm PC.**

## Simpler downloads (2026-11-12)

- Premiere downloads renamed `FilmEffects-Premiere-*` (next to `FilmEffects-Resolve-*`).
- One Windows Premiere build: the Setup installs the CUDA build only (it contains the CPU path and
  falls back automatically); the CPU-only build is still compiled in CI but no longer shipped, and
  the manual ZIPs are gone. CI now also checks that the `.aex`/`.ofx` do not import `nvcuda.dll`,
  i.e. they load on PCs without an NVIDIA driver (the CUDA-built `.ofx` already loaded on the
  driver-less Windows CI machine). Not host-tested on a non-NVIDIA PC.
- Installers stay unsigned by the owner's choice (hobby project, public source).

## DaVinci Resolve — first host test on macOS (2026-11-12)

- **User-tested v1.0.9 in DaVinci Resolve on macOS: "works well".** macOS asked to allow the
  unsigned installer in System Settings → Privacy & Security (expected: ad hoc signed, not
  notarized). Detailed checks (Metal vs CPU mode, presets, export, comparison with Premiere) were
  not reported separately.
- Windows Resolve (CUDA) and Linux: still untested in Resolve. Windows showed the expected
  unsigned-installer warning.

## DaVinci Resolve (OpenFX) — milestone 2: GPU rendering (2026-11-11)

- v1.0.8 published the CPU-only Resolve downloads (Windows Setup, macOS pkg, Linux tarball).
- GPU engines take the OpenFX frame layout (`rgba`, `bottomUp`; grain rows still counted from the
  top, so CPU and GPU frames match). `tests/gpu` checks the OpenFX layout against the CPU
  reference (worst difference 0/255 at 320x180); deliberately wrong channel order or missing flip
  makes 26 / 32 cases fail, so the test is effective.
- OFX plug-in declares Metal (macOS) and CUDA (Windows, default stream, no CUDA streams) render
  support and runs the Premiere engines (`FilmEffectsOFXMetal.mm`, `FilmEffectsOFXCuda.cpp`).
- CI run `43105983642`: Windows CUDA 12.9.1 build with sm_61 device code and static runtime,
  NVIDIA EULA shipped by the Setup; macOS bundle contains `FilmEffects.metallib`; Linux CPU only.
  Linker warning LNK4098 (LIBCMT from the static CUDA runtime) also noted; harmless in practice
  but worth watching.
- **Still not run in DaVinci Resolve, CPU or GPU.**

## DaVinci Resolve (OpenFX) — milestone 1: CPU plug-in (2026-11-11)

- User asked to start the DaVinci Resolve version after the installer fix release.
- `src/hosts/ofx/FilmEffectsOFX.cpp`: one `FilmEffects.ofx.bundle` with four effects
  (`com.venostavisuals.FilmEffects|FilmBloom|FilmHalation|FilmGrain`), parameters generated from the
  shared table (`FilmEffectsParams.h`, OFX names `fe<diskId>`), presets, bypass via isIdentity,
  CPU render through the tested core (rows flipped to top-first so grain matches Premiere).
- Built with the BSD OpenFX Support library fetched at a pinned commit (`src/hosts/ofx/ofx.cmake`).
- Verified locally on Linux: builds; `fe_ofx_load_test` loads the bundle and finds all four effects.
- CI `film-effects-ofx.yml`: macOS universal `.pkg`, Windows Setup (silent install/uninstall test),
  Linux tarball; release attaches them.
- **Not tested in DaVinci Resolve.** No render test through a real OFX host yet (OpenFX's demo
  host is 8-bit only). GPU (CUDA/Metal via OFX GPU extensions) is the next milestone; the GPU
  engines currently assume BGRA and need an RGBA option.

## Windows installer folder fix (2026-11-11)

- **Bug found by the user on Windows (Premiere 2027):** v1.0.6 Setup installed into
  `Common\Plugins\7.0\MediaCore` (no hyphen); Premiere scans `Common\Plug-ins\7.0\MediaCore`
  (registry `HKLM\SOFTWARE\Adobe\Premiere Pro\CurrentVersion` → `Plug-InsDir`). The effects
  did not load. The uninstaller also landed in that wrong folder.
- Fix: Setup reads `Plug-InsDir` (falls back to the hyphenated path), `UsePreviousAppDir=no`,
  uninstaller moved to `Program Files\Venosta Visuals\Film Effects`, and upgrades delete the
  stray v1.0.6 files/folders. CI now runs the installer silently on Windows (fallback folder,
  stray cleanup, registry folder, uninstall).
- User is copying the `.aex` files by hand to test loading/rendering; results pending.

## Windows Setup.exe installer (2026-11-11)

- User asked for something better than the PowerShell install script. Added an Inno Setup 6
  installer (`installer/windows/FilmEffects.iss`) bundling the CPU and CUDA builds; CI job
  `windows-installer` in `film-effects-plugins.yml` compiles it, and releases attach
  `FilmEffects-Windows-Setup-<tag>.exe` (ZIPs kept as manual alternative).
- Pre-selects CUDA when `nvcuda.dll` exists, else CPU-only; admin install into the MediaCore
  folder; uninstall entry in Settings → Apps; unsigned (SmartScreen warning expected).
- **Status:** compiled in CI only; not yet run on a Windows PC. User will test on the GTX 1080 PC.

## Windows CUDA release published (2026-11-11)

- [**v1.0.5**](https://github.com/d1anarene/venosta-visuals-plugins/releases/tag/v1.0.5)
  includes `FilmEffects-Windows-CUDA-experimental-v1.0.5.zip` alongside the CPU-only Windows
  fallback, Mac installer and Track Range Exporter CCX downloads. Release workflow
  `43051349682` passed and all five assets were verified in GitHub's release response.
- Corrected candidate `d91878c`: native run `43050168045` passed all three builds; push tests
  `43050168151` and PR tests `43050231863` both passed all six jobs before PR #8 was merged.
- All four CUDA plugins compiled/linked and passed checks for `xGPUFilterEntry`, no dynamic
  cudart DLL dependency, and Pascal `sm_61` device code. The ZIP includes NVIDIA license/notices.
- CUDA 12.9.1 is intentional: the user owns a GTX 1080 (Pascal), which CUDA 13 cannot target.
  The build also includes sm_75, sm_86, sm_89 and forward PTX for compatible newer NVIDIA GPUs;
  it is not restricted to the user's card. CPU-only remains a separate download.
- **No NVIDIA runtime or Windows Premiere host test was performed.** The user will test at home.
  DaVinci Resolve integration remains unimplemented; no Resolve build is claimed.

## Windows CUDA packaging history (2026-11-11)

- User requests a CUDA-enabled Windows download for home testing, alongside the CPU fallback.
- Existing CUDA engine and Premiere entry point were present, but v1.0.3 explicitly built with
  `FE_WITH_CUDA=OFF`. The Linux CUDA compile job did not build the Windows Premiere adapter.
- Added a separate `windows-2022` / VS 2022 job installing CUDA 12.9.1 compiler, runtime and VS
  integration (pinned cuda-toolkit action v0.2.36). CUDA is mandatory in this job; CPU download
  remains separate. Inspect every `.aex` for `xGPUFilterEntry` and reject dynamic cudart imports.
- First Windows CUDA job at `80f7d8a` compiled and linked all four plugins, verified GPU entry
  exports and static runtime linkage (run `43048689025`). Final GTX 1080-targeted job at
  `a79d9b4` failed configuration: PowerShell unwrapped a single-item conditional array, then
  splatted the architecture-list string into characters (run `43048994443`).
- PR #7 was merged before that failure was correctly caught. Its release cannot publish unless
  native builds pass. Follow-up fixes argument construction with one explicit array.
- Final CUDA package and runtime verification pending. User hardware: **GTX 1080**.
  Explicit native `sm_61` target added; inspect CUDA ELF sections for Pascal code in each plugin. No NVIDIA hardware is available
  here; successful compilation will not establish GPU execution or Premiere compatibility.
- DaVinci Resolve audit: no OpenFX adapter, build target, tests or installer exists in this repo.
  No Resolve port has been implemented or tested. Its integration remains separate future work.

## Published release (2026-11-11)

- [**v1.0.3**](https://github.com/d1anarene/venosta-visuals-plugins/releases/tag/v1.0.3)
  published as a stable release from merge commit `49f5307` via PR #6.
- Final release candidate `930fbcf`: push tests `43044044530`, PR tests `43044067346` and native
  builds `43044044213` **all passed**. Includes CUDA compilation, CPU tests on three OSes,
  Metal GPU comparison and Track Range Exporter checks.
- Release workflow `43044645114` **passed**; verified both Film Effects assets are attached:
  `FilmEffects-mac-v1.0.3.pkg` and `FilmEffects-Windows-CPU-experimental-v1.0.3.zip`.
  Existing Track Range Exporter Premiere and Media Encoder CCX downloads are also included.
- Windows CPU package is explicitly experimental and awaits the user's at-home Premiere test.
  Mac host verification remains the limited user smoke test documented below. No claim of
  export matching, long-run stability, HDR verification or NVIDIA GPU execution is made.

## First Mac release (2026-11-11)

- **User host verification:** after the corrected installer, a Premiere screenshot shows
  Film Effects, Film Bloom, Film Halation and Film Grain under Venosta Visuals. The user reports
  satisfactory operation and approves release. Environment: Premiere Pro 27, macOS 27.2,
  Apple Silicon (architecture/version inspected locally; Mac model not recorded).
- This is a limited user smoke test, not completion of the manual acceptance checklist.
  Export matching, CPU-vs-Metal inside Premiere, HDR, saved-project recall, long timeline
  stability, Intel host behavior and measured performance remain unverified.
- Repository version moves to `1.0`. Release workflow builds native plugins with private SDKs,
  includes a reproducible Mac `.pkg`, and labels the Windows CPU ZIP experimental. No SDK
  sources are included. The Mac package is ad hoc signed only; installer unsigned/not notarized.
- CUDA CI initially retried the official Ubuntu archive, but that hostname also failed DNS
  resolution (run `43042608039`). NVIDIA downloads worked. Updated setup to extract the compiler,
  headers and runtime from NVIDIA-only packages into a private toolkit directory; existing runner
  C++ tools are reused. Compilation remains mandatory; no failure is suppressed.

## Release preparation checks (2026-11-11)

- Local `./scripts/run-core-tests.sh`: **19,931 checks passed, 0 failed**.
- GPU pass emulation: **96 cases passed**; half conversion: **0 mismatches**.
- `scripts/package-macos.sh` built a package from the existing CI artifact; verified universal
  architectures, signatures and PiPL resources. Installer execution is not part of this check.
- Updated workflow YAML parses successfully; shell syntax and `git diff --check` pass.
- Native CI run `43042607974`: Mac and Windows builds passed. Downloaded the generated Mac
  installer: receipt version `1.0.5`, all four bundle versions `1.0.0`, correct destination and
  valid ad hoc signatures. All six jobs passed on commit `bf00dfd` in CI runs `43043682334` (push) and
  `43043692491` (PR), including CUDA compilation using NVIDIA-only dependency extraction.
- User explicitly requests the experimental Windows CPU download for testing at home. Include
  it in the release, labelled host-unverified; Windows CUDA acceleration is not in that download.

## Latest build and preview packaging (2026-11-11)

- Native build [run 43034676457](https://github.com/d1anarene/venosta-visuals-plugins/actions/runs/43034676457)
  at `22a5199`: **macOS and Windows succeeded**, producing all four effects.
- Tests [run 43034676360, attempt 2](https://github.com/d1anarene/venosta-visuals-plugins/actions/runs/43034676360):
  CPU on Linux/macOS/Windows, Metal GPU comparison and Track Range Exporter **passed**.
  CUDA installation failed again resolving `azure.archive.ubuntu.com`, before CUDA compilation.
- Created local `FilmEffects-mac-preview-22a5199.pkg` from the successful Mac artifact.
  All four bundles contain arm64 + x86_64; embedded minimum OS is **macOS 26.0**.
  Restored executable permissions lost by artifact ZIP packaging; ad hoc signed and verified
  each bundle. Installer is unsigned and not notarized. Existing bundles are backed up outside
  Adobe's plugin scan directory before updating. No Adobe SDK files are included.
- Package expanded and payload/signatures checked locally. **Installer execution, Premiere
  loading/rendering, and export remain unverified**. No installation was performed in this chat.
- Historical "unbuilt" / "blocked" labels below describe earlier work; the successful native CI
  build supersedes them. Stage 1 still needs its first Premiere host proof.

## First installation diagnosis and corrected package (2026-11-11)

- User installed the first preview; the package receipt and all four bundles were present.
  Premiere's Effects panel did not show Venosta Visuals.
- Read the local Premiere 27.0 `Plugin Loading.log`: it scans
  `/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore`, while the first installer
  placed files in `Common/Plugins` (no hyphen). None of our bundles appeared in its scan log.
- Corrected Mac installation scripts and documentation to use `Common/Plug-ins`.
  Repackaged the same binaries as `FilmEffects-mac-preview-22a5199-fixed.pkg`, receipt version
  `0.1.0.5`. Expanded and checked the corrected destination, resources, executable permissions
  and signatures for all four bundles; PiPL resource decoded successfully with `DeRez -useDF`.
- **Corrected installer not yet executed; Premiere loading and rendering still unverified.**
  Old inactive files are left intact; no preferences, projects or plugin cache were changed.

## Verification legend
- **tested**: compiled and run here (`scripts/run-core-tests.sh`, Linux, g++ 13)
- **syntax**: compiled only against a throwaway stand-in for the SDK headers (not committed; proves
  only that our own C++ is well-formed)
- **ci-gpu**: compiled with Apple's Metal toolchain and run on a GPU in GitHub Actions
  (`macos-latest`, "Apple Paravirtual device"), compared with the CPU reference
- **unbuilt**: written, never compiled
- **host**: limited user smoke test in Premiere Pro 27 on Apple Silicon; detailed checklist pending

## Stage status

| Stage | State |
| --- | --- |
| 0 Feasibility audit | ✅ `docs/FEASIBILITY.md` |
| 1 Native plugin built + run in Premiere | ✅ Native builds in CI; user reports Mac operation satisfactory and confirms all four effects load. Detailed acceptance checks pending. |
| 2 CPU cores + tests + CLI | ✅ tested — 37 test cases, ~19.8k checks pass |
| 3 Native GPU effects | 🟡 Metal engine verified against CPU in CI; native Metal entry builds. GPU activation inside Premiere and CUDA hardware execution unverified. |
| 4 Usability | ✅ First Mac installer and all four effects user-tested; detailed preset/keyframe/export checks pending. |
| 5 Optional UXP companion | not started (only after Stage 4 works in host) |

## Earlier development record (historical verification labels)

The entries below retain the original verification history; current status is above.

## Done
- Core: params + sanitising + 7 presets; pixel I/O (BGRA 8u/32f, ARGB 8u/16u/32f premultiplied, strides incl. negative) — tested
- Bloom (soft knee, 5/6-scale pyramid), Halation (edge-preferring warm spill), Grain (hashed multiscale value noise, luminance response, luma/colour) — tested
- Determinism: same frame/seed → identical pixels; render order independent; threaded CPU path bit-identical — tested
- Bypass bit-exact; alpha untouched; NaN/Inf/HDR/negative safe; odd sizes; 1080p/2160p — tested
- Shared CPU/GPU maths header (`fe_kernels.h`), shared blur plans and Gaussian weights
- Native effect (`FilmEffects.cpp`, PiPL) — **syntax**; Premiere GPU entry — **unbuilt**
- Metal kernels + `MetalFilmEngine` — **ci-gpu**: max difference vs CPU < 0.001/255 (32f frames),
  ≤ 0.062/255 (16f frames), mean ≤ 0.019/255; tolerance 2/255 (see tests/gpu/README.md)
- CLI renderer, synthetic fixtures, A/B images, CPU benchmarks (docs/PERFORMANCE.md)
- Parameter mapping and host time → grain key — tested
- Standalone effects Film Bloom / Film Halation / Film Grain: same sources built per variant,
  one parameter table (stable disk IDs), bit-identical to the combined effect — table and
  equivalence tested; host code **syntax** for all 4 variants
- CUDA backend (Windows/NVIDIA): shared per-pixel pass functions + orchestration, CUDA kernels,
  engine, Premiere CUDA GPU entry. Pass sequence verified exact vs CPU reference in emulation
  (tested, every platform); kernels compile with nvcc in CI; **not yet run on an NVIDIA GPU**;
  GPU entry **unbuilt**
- Windows: CMake builds `.aex` plugins (PiPLtool step), PowerShell build/install scripts — **unbuilt**;
  core compiled with MSVC and tested in CI (`film-effects-core-windows`)
- CPU memory cut by more than half and CPU path faster (docs/PERFORMANCE.md); output bit-identical — tested

## Checked against the real Adobe SDKs (2026-11-11, AE SDK 27.0 + Premiere Pro 27.0 C++ SDK)
- CPU entry `FilmEffects.cpp`: compiles (syntax) against the real SDK headers for all 4 variants
  (only stand-ins for 3 macOS system headers). Fixed: `PF_REGISTER_EFFECT_EXT2` needs a local
  `result` (as in SDK_Noise).
- CUDA GPU entry: compiles against the real `PrGPUFilterModule.h`. Fixed: AE headers must be
  included before it (as in SDK_ProcAmp). Default CUDA stream like SDK_CrossDissolve's CUDA sample.
- GPU params now read by `PrParam.mType` (samples use mFloat32/mFloat64/mBool); popup numbering in
  the GPU path is undocumented, so 0-based numbering is detected (FilmEffectsGPUParams.h).
- Not yet: Metal GPU entry (needs a Mac compiler), PiPL via Rez/PiPLtool, linking, running.

## Blocked / needs a person with a Mac
1. `./scripts/build-macos.sh <AE SDK Examples> <Premiere SDK>` — fix any SDK naming differences
   (listed in docs/FEASIBILITY.md, "verify against your SDK").
2. Install, run `tests/premiere-manual/CHECKLIST.md` section A (passthrough, CPU vs GPU).
3. Check that `FilmEffectsGPU.mm` compiles against the SDK's `PrGPUFilterModule.h` and that Premiere
   uses the GPU path (Metal renderer) — the engine underneath is already verified in CI.

## Next (after host proof)
- Run the CUDA engine on an NVIDIA GPU (add a cuda_vs_cpu comparison tool like metal_vs_cpu)
- Optional UXP preset panel (Stage 5)

## Commands run (this session)
```
./scripts/run-core-tests.sh                -> 19781 checks passed, 0 failed
./build/fe_render ... --bench 7            -> docs/PERFORMANCE.md
./scripts/make-fixtures.sh                 -> 56 images
g++ -fsyntax-only (SDK stand-in) FilmEffects.cpp -> OK
clang++ build of core (Linux)               -> no warnings, all checks pass
GitHub Actions run 42899553518             -> core tests on Linux + macOS, Metal build, GPU vs CPU: PASS
```
