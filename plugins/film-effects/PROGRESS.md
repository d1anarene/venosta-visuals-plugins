# PROGRESS — Film Effects

_Last update: 2026-11-11_

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
