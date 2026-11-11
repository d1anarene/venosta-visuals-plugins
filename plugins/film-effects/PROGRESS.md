# PROGRESS — Film Effects

_Last update: 2026-11-11_

## Verification legend
- **tested**: compiled and run here (`scripts/run-core-tests.sh`, Linux, g++ 13)
- **syntax**: compiled only against a throwaway stand-in for the SDK headers (not committed; proves
  only that our own C++ is well-formed)
- **ci-gpu**: compiled with Apple's Metal toolchain and run on a GPU in GitHub Actions
  (`macos-latest`, "Apple Paravirtual device"), compared with the CPU reference
- **unbuilt**: written, never compiled
- **host**: verified inside Premiere Pro — **nothing yet**

## Stage status

| Stage | State |
| --- | --- |
| 0 Feasibility audit | ✅ `docs/FEASIBILITY.md` |
| 1 Thin native plugin compiled + run in Premiere | ⛔ **Blocked**: no Mac, Adobe SDKs or Premiere in this environment. Plugin source is ready to build. |
| 2 CPU cores + tests + CLI | ✅ tested — 37 test cases, ~19.8k checks pass |
| 3 Native Metal effects | 🟡 Metal kernels + engine **ci-gpu verified** (matches CPU on 8 scenes × 6 presets, 32f and 16f); Premiere GPU entry point (`FilmEffectsGPU.mm`) **unbuilt** |
| 4 Usability (groups, presets, standalone effects, docs) | 🟡 params/groups/presets + 3 standalone effects written (**syntax** for CPU entry); docs done; host tests pending |
| 5 Optional UXP companion | not started (only after Stage 4 works in host) |

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
- CPU memory cut by more than half and CPU path faster (docs/PERFORMANCE.md); output bit-identical — tested

## Blocked / needs a person with a Mac
1. `./scripts/build-macos.sh <AE SDK Examples> <Premiere SDK>` — fix any SDK naming differences
   (listed in docs/FEASIBILITY.md, "verify against your SDK").
2. Install, run `tests/premiere-manual/CHECKLIST.md` section A (passthrough, CPU vs GPU).
3. Check that `FilmEffectsGPU.mm` compiles against the SDK's `PrGPUFilterModule.h` and that Premiere
   uses the GPU path (Metal renderer) — the engine underneath is already verified in CI.

## Next (after host proof)
- Windows: CUDA path (or software only first)
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
