# PROGRESS — Retro

_Last update: 2026-11-12_

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
- **Not tested in Premiere or Resolve.**

## Next

- CI results (Metal vs CPU on a Mac GPU, CUDA compile, installers).
- Host tests by the user.
