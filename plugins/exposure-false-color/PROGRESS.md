# PROGRESS — Exposure & False Color

_Last update: 2026-11-13_


## Host test: macOS and Windows (2026-11-13, owner)

- v1.0.17, Windows PC with NVIDIA GPU: Premiere Pro 2027 and DaVinci Resolve 21; Apple Silicon Mac:
  Premiere Pro 2027 and DaVinci Resolve 21.2. Reported: everything worked (installers, effect in
  the effects list, rendering, controls, presets).
- Not yet tested: exports; Windows on Arm; Linux. Windows GPU renderer: the project was presumably
  set to CUDA, not confirmed.

## Host testing confirmed by the owner (2026-11-13)

- Owner confirms the same host-testing status as Film Effects: **user-tested in Premiere Pro 27
  and DaVinci Resolve on macOS (Apple Silicon)**. No longer marked experimental on macOS.
- Existing CPU tests, GPU comparisons and CI build/installer checks remain documented below.
- Windows (including Windows on Arm) and Linux host operation remain unverified. Export matching,
  long timeline stability, HDR workflows and performance measurements remain unverified.
- Earlier entries below retain their verification status at the time; this section is current.

## Milestone 1: complete effect, CPU + GPU (2026-11-12)

- Roadmap item 3. One effect on the shared engine for Premiere Pro and DaVinci Resolve.
- Design: false colour measured in stops from middle grey (working-space independent), own band
  layout; zebras (two levels, diagonal stripes in opposite directions), clipping, split view with
  divider, legend bar; opacity; presets. Viewing aid — documented as "turn off before export".
- The whole effect is one per-pixel function (`ef_kernels.h`, C++/CUDA/Metal) with a single
  parameter struct, so CPU and GPU cannot drift apart. CUDA pass (`EfCuda.cu`), Metal kernel on the
  shared MetalBackend, renderers for both hosts.
- Tests (11, ~133,000 checks, all pass locally): band boundaries, middle-grey parameter, same light
  in sRGB/Linear spaces, zebra coverage/direction/ranges, clipping, split geometry, legend, opacity/
  alpha/transparency, **GPU pass in Premiere and OpenFX layouts bit-identical to the CPU**, parameter
  table/presets/effect interface. Metal-vs-CPU test for CI (`tests/gpu/ef_metal_vs_cpu.mm`).
- Visual check (CPU render of the synthetic night scene with split view): plausible.
- CI (commit 625988e): `exposure-false-color.yml` green on the first run — Premiere (macOS Metal,
  Windows CUDA, Windows on Arm) with installers and installer tests, Resolve on all four
  platforms; **Metal vs CPU on a Mac GPU: 120 cases, worst 8.9e-8 (32f), 0.059/255 (16f), no
  band-boundary pixels**.
- **Not tested in Premiere or Resolve.**
