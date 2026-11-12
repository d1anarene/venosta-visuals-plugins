# Retro — specification for coding agents

> Plugin 5 of the Venosta Visuals collection (see `ROADMAP.md`). Built on the shared engine
> (`shared/engine`): one effect for **Premiere Pro** and **DaVinci Resolve (OpenFX)**, CPU reference +
> Metal + CUDA. Repository rules: root `AGENTS.md`.

## Mission

Analog video looks from general signal-processing principles (no proprietary code or assets; not a
recreation of any specific product), in one effect with sections that can be switched on and off:

1. **Color Bleed** — the picture is processed like a composite/tape video signal in Y'UV (encoded
   values): luma bandwidth limited (horizontal low-pass) with edge-enhancement ringing, chroma
   strongly low-passed horizontally and shifted, chroma saturation control.
2. **Tape** — per-line horizontal jitter (smooth + random), a tracking-noise band rolling through
   the frame, dropouts (short bright horizontal streaks), head-switching distortion in the bottom
   lines.
3. **Noise** — luma and chroma noise ("snow"), per frame.
4. **CRT** — scanlines (line count relative to the frame), aperture-grille or shadow-mask RGB
   pattern, screen curvature (barrel) with dark corners.

## Requirements

- Identifiers (never change after release): Premiere match name `VVIS Retro`, OpenFX id
  `com.venostavisuals.Retro`, OpenFX parameter prefix `rt`, disk IDs append-only.
- Deterministic per (seed, frame key, frame size, parameters); animated per frame; any frame renders
  identically in any order on CPU and GPU (engine hashing rules).
- Resolution independence: horizontal sizes in pixels of a 1080-line frame, scanline count in lines
  per frame height, so HD/4K/previews look alike.
- Alpha unchanged, transparent pixels untouched, global mix, bit-exact bypass, no NaN/Inf.
- CPU reference with tests; GPU through the shared engine checked against it (emulation in both host
  layouts, Metal on a Mac GPU in CI).
- Honesty: never claim host-tested behaviour that was not tested in a host.
