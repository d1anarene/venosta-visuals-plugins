# PROGRESS — Film Effects

_Last update: 2026-11-10_

## Verification legend
- **tested**: compiled and run here (`scripts/run-core-tests.sh`, Linux, g++ 13)
- **syntax**: compiled only against a throwaway stand-in for the SDK headers (not committed; proves
  only that our own C++ is well-formed)
- **unbuilt**: written, never compiled
- **host**: verified inside Premiere Pro — **nothing yet**

## Stage status

| Stage | State |
| --- | --- |
| 0 Feasibility audit | ✅ `docs/FEASIBILITY.md` |
| 1 Thin native plugin compiled + run in Premiere | ⛔ **Blocked**: no Mac, Adobe SDKs or Premiere in this environment. Plugin source is ready to build. |
| 2 CPU cores + tests + CLI | ✅ tested — 32 test cases, ~19.8k checks pass |
| 3 Native Metal effects | 🟡 kernels + engine + GPU entry written (**unbuilt**); CPU/GPU comparison pending |
| 4 Usability (groups, presets, docs) | 🟡 params/groups/presets written (**syntax** for CPU entry); docs done; host tests pending |
| 5 Optional UXP companion | not started (only after Stage 4 works in host) |

## Done
- Core: params + sanitising + 7 presets; pixel I/O (BGRA 8u/32f, ARGB 8u/16u/32f premultiplied, strides incl. negative) — tested
- Bloom (soft knee, 5/6-scale pyramid), Halation (edge-preferring warm spill), Grain (hashed multiscale value noise, luminance response, luma/colour) — tested
- Determinism: same frame/seed → identical pixels; render order independent; threaded CPU path bit-identical — tested
- Bypass bit-exact; alpha untouched; NaN/Inf/HDR/negative safe; odd sizes; 1080p/2160p — tested
- Shared CPU/GPU maths header (`fe_kernels.h`), shared blur plans and Gaussian weights
- Native effect (`FilmEffects.cpp`, PiPL) — **syntax**; GPU entry + Metal engine/kernels — **unbuilt**
- CLI renderer, synthetic fixtures, A/B images, CPU benchmarks (docs/PERFORMANCE.md)
- Parameter mapping and host time → grain key — tested

## Blocked / needs a person with a Mac
1. `./scripts/build-macos.sh <AE SDK Examples> <Premiere SDK>` — fix any SDK naming differences
   (listed in docs/FEASIBILITY.md, "verify against your SDK").
2. Install, run `tests/premiere-manual/CHECKLIST.md` section A (passthrough, CPU vs GPU).
3. Build the GPU comparison harness (`tests/gpu/README.md`), record tolerances.

## Next (after host proof)
- Reduce CPU memory (reuse planes, process bloom/halation at reduced precision where safe)
- Standalone entries Film Bloom / Film Halation / Film Grain (same core, separate match names)
- Windows: CUDA path (or software only first)
- Optional UXP preset panel (Stage 5)

## Commands run (this session)
```
./scripts/run-core-tests.sh                -> 19781 checks passed, 0 failed
./build/fe_render ... --bench 7            -> docs/PERFORMANCE.md
./scripts/make-fixtures.sh                 -> 56 images
g++ -fsyntax-only (SDK stand-in) FilmEffects.cpp -> OK
```
