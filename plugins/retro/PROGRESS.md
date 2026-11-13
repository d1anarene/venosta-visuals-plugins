# PROGRESS — Retro

_Last update: 2026-11-13_

## Host testing confirmed by the owner (2026-11-13)

- Owner confirms the same host-testing status as Film Effects: **user-tested in Premiere Pro 27
  and DaVinci Resolve on macOS (Apple Silicon)**. No longer marked experimental on macOS.
- Existing CPU tests, GPU comparisons and CI build/installer checks remain documented below.
- Windows (including Windows on Arm) and Linux host operation remain unverified. Export matching,
  long timeline stability, HDR workflows and performance measurements remain unverified.
- Earlier entries below retain their verification status at the time; this section is current.

## Milestone 1: complete effect, CPU + GPU (2026-11-12)

- Roadmap item 4. One effect on the shared engine for Premiere Pro and DaVinci Resolve.
- Design: encoded RGB → BT.601 Y'UV; three horizontal-only Gaussian blurs (luma softness, wider
  luma for ringing = edge overshoot, chroma); per output pixel `rt_geometry` (CRT barrel
  curvature, tape line jitter, rolling tracking band, head switching) and `rt_finish` (ringing,
  chroma shift/saturation, tracking noise, dropouts, luma/chroma noise, scanlines, aperture grille
  / shadow mask, screen edge, mix). Both functions are shared verbatim by CPU, CUDA and Metal.
- Scanlines are averaged analytically over each pixel's height, so line counts finer than the
  pixel grid darken evenly instead of producing moiré (found and fixed in a render review).
- Deterministic per seed + frame key (engine hash); Animate off freezes the pattern.
- GPU: `rt_gpu_pipeline.h` over any backend (prepare → 3× `Pyramid::blurHorizontal` →
  composite), CUDA engine, Metal engine on the shared MetalBackend, renderers for both hosts.
- Tests: 14 core tests (~132,800 checks) — Y'UV round trip, bit-exact bypass, neutral bleed =
  identity, chroma bleeds horizontally but not vertically, chroma shift direction, ringing
  over/undershoot, determinism and animation, noise zero-mean, jitter + head switching, CRT
  scanline contrast / fine-line anti-aliasing / mask energy / curvature, resolution
  independence, alpha + transparency + extreme settings finite, mix linearity, parameter table +
  every preset through the panel. GPU pass emulation: 360 cases (all scenes × presets × 3 frame
  keys × Premiere 32f/16f and OpenFX layouts): **32f and OpenFX identical to the CPU, 16f within
  0.125/255**; a sabotaged kernel is detected.
- Metal-vs-CPU test for CI: `tests/gpu/rt_metal_vs_cpu.mm`.
- CI (commits 138594e / e6bb34f): green on the first run — core tests on Linux, macOS and
  Windows; `retro.yml` Premiere (macOS Metal, Windows CUDA, Windows on Arm) with installers and
  installer tests, Resolve on all four platforms; **Metal vs CPU on a Mac GPU: 120 cases, worst
  2.4e-7 (32f), 0.125/255 (16f), 2.4e-7 (OpenFX layout), no pixels outside tolerance**.
- **Not tested in Premiere or Resolve.**

## Next

- Windows/Linux host tests; detailed export, stability, HDR and performance checks.
