# PROGRESS — Lens & Diffusion

_Last update: 2026-11-12_

## Milestone 2: GPU renderers (2026-11-12)

- First CI run of milestone 1 (`lens-diffusion.yml`, commit 2d2a59e): **all green** — Premiere
  builds (macOS, Windows x64, Windows on Arm), installers and installer tests, Resolve bundles on
  all four platforms with load tests.
- GPU pass sequence `src/gpu/common/ld_gpu_pipeline.h` (prepare, chromatic aberration, weights,
  diffusion via the shared GPU pyramid, streaks via the new shared x-only pyramid, composite),
  written once for any backend: CUDA (`LensCudaEngine.cu` on the shared `vv_cuda_backend.cuh`),
  Metal (`LensDiffusion.metal` + `LensMetalEngine.mm` on the shared `MetalBackend` and
  `vv_common.metal`) and the CPU emulation (shared `vv_gpu_emulation.h`).
- `tests/gpu/ld_gpu_emulation_test.cpp`: 144 cases (8 scenes x 6 presets x BGRA 32f / BGRA 16f /
  RGBA bottom-up): worst 0/255 (32f), 0.125/255 (16f), 0.015/255 (OpenFX layout). Deliberately
  swapping the red/blue aberration direction fails 20 cases.
- `tests/gpu/ld_metal_vs_cpu.mm` (Metal on a Mac GPU vs CPU) and a CUDA compile job added to CI.
- Renderers for both hosts (`LensMetal.mm`, `LensCuda.cpp`) with up-front `prepare()` so a missing
  metallib / CUDA failure falls back to the CPU in Premiere.
- **Not yet run on a GPU inside a host.**

## Milestone 1: CPU effect on the shared engine (2026-11-12)

- Owner chose Lens & Diffusion as plugin 2 of the roadmap and a shared engine for all plugins.
- Core (`src/core`): diffusion (energy-conserving scatter with a three-Gaussian PSF, highlight bias
  from mist to black-mist style, warmth), vignette (natural cos⁴ or smooth, darken/lighten),
  lateral chromatic aberration (red/blue radial Catmull-Rom resampling, green fixed), anamorphic
  streaks (soft-knee highlights, horizontal-only pyramid blur added to the engine), global mix.
- Per-pixel maths in `ld_kernels.h` (C++/CUDA/Metal-compatible) for the coming GPU path.
- Effect (`src/effect`): parameter table (disk IDs 1–33), presets, `vv::Effect`; Premiere and
  OpenFX through the shared adapters; installers from the shared templates; CI through the shared
  workflows (`lens-diffusion.yml` → `vv-premiere.yml`, `vv-ofx.yml`).
- Tests (`tests/core`, 11 tests, ~34,000 checks): bypass exactness, energy conservation of
  diffusion, mist vs black-mist shadows, neutral/warm tint, CA zero in the centre and growing at
  the edges with green unchanged, streaks horizontal and only from highlights, vignette symmetry
  and sign, alpha/transparency/mix/odd sizes, every preset finite on every scene, parameter table
  and effect interface. All pass locally (Linux).
- Visual check: CPU renders of the synthetic night scene look plausible. Note: large bright areas
  (lit windows) give broad streak bands — physically consistent but worth tuning after host tests.
- Not tested in Premiere or Resolve.

## Next

1. CI green with the GPU renderers (Metal vs CPU on a Mac GPU, CUDA compile, host bundles).
2. Host tests by the owner; tune streaks / diffusion looks.
