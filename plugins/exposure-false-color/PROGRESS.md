# PROGRESS — Exposure & False Color

_Last update: 2026-11-12_

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
