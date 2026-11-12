# PROGRESS — Lens & Diffusion

_Last update: 2026-11-12_

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
- **Not yet built in CI at the time of writing; not tested in Premiere or Resolve; CPU only.**

## Next

1. CI green on all platforms (first build of the Premiere/Resolve binaries).
2. GPU renderers (Metal, CUDA) via the shared GPU framework; GPU-vs-CPU tests.
3. Host tests by the owner; tune streaks / diffusion looks.
