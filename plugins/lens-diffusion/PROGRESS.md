# PROGRESS — Lens & Diffusion

_Last update: 2026-11-13_

## Host testing confirmed by the owner (2026-11-13)

- Owner confirms the same host-testing status as Film Effects: **user-tested in Premiere Pro 27
  and DaVinci Resolve on macOS (Apple Silicon)**. No longer marked experimental on macOS.
- Existing CPU tests, GPU comparisons and CI build/installer checks remain documented below.
- Windows (including Windows on Arm) and Linux host operation remain unverified. Export matching,
  long timeline stability, HDR workflows and performance measurements remain unverified.
- Earlier entries below retain their verification status at the time; this section is current.

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
  CI run 43207704948: **Metal vs CPU 96 cases, worst 0.0001/255 (32f), 0.125/255 (16f)**; CUDA
  engine compiles with nvcc; `lens-diffusion.yml` (43207704935) green with Metal + CUDA builds.
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

## Exaggerated chromatic aberration (2026-11-12, owner request)

- Amount range widened from ±10 to ±300 px (slider ±50), same parameter, saved values unchanged.
- New control **Aberration Spectral Smear** (disk ID 34, appended at the end of the table so all
  existing controls keep their positions): 0 = the classic two-colour fringe (bit-identical, test),
  > 0 = weighted gather of up to 65 samples along the radial displacement with Gaussian channel
  lobes → continuous rainbow smear; sample count grows with the local shift (≤ 1 px spacing until
  the cap). Same maths on CPU, CUDA and Metal (`ld_kernels.h`); GPU emulation matches the CPU.
- New preset **Extreme Fringe** (appended to the preset menu).
- Tests: neutral grey stays neutral, smear spreads light continuously, ±300 px finite, smear 0 is
  bit-identical to before. **Not tested in a host.**

## Next

1. CI green with the GPU renderers (Metal vs CPU on a Mac GPU, CUDA compile, host bundles).
2. Windows/Linux host tests; detailed export, stability, HDR and performance checks.
3. Tune streaks / diffusion looks based on owner feedback.
