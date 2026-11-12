# Exposure & False Color — specification for coding agents

> Plugin 4 of the Venosta Visuals collection (see `ROADMAP.md`). Built on the shared engine
> (`shared/engine`): one effect for **Premiere Pro** and **DaVinci Resolve (OpenFX)**, CPU reference +
> Metal + CUDA. Repository rules: root `AGENTS.md`.

## Mission

Exposure **viewing aids** for colourists and editors — the overlays expensive monitoring tools and
plugins offer, as one free effect on an adjustment layer:

1. **False Color** — every pixel coloured by its exposure, measured in **stops relative to middle
   grey** (18 % linear in the chosen working space), so the reading is independent of the working
   space. Our own band layout (documented in the README with a legend), not a copy of a product's:
   crushed (purple) · deep shadows (blue, teal) · shadows/mids (grey ramp of the pixel's own
   luminance) · middle grey ±0.3 stop (green) · skin highlight +0.8…+1.4 stops (pink) · near
   white (yellow) · at/above diffuse white = clipping in SDR or HDR highlight (red).
2. **Zebras** — diagonal stripes where the encoded luma is at or above a threshold (default 95 %),
   optional second zebra band (e.g. 70–80 % for skin).
3. **Clipping** — pixels with a channel at/above 1.0 (red) or at/below 0.0 (blue) in the encoded
   signal.
4. **Split** — show the overlays only on one side of a movable divider (vertical or horizontal),
   with a thin divider line, to compare the real image with the analysis.
5. **Legend** — a small false-colour bar at the bottom of the frame.

## Requirements

- Identifiers (never change after release): Premiere match name `VVIS ExposureFalseColor`, OpenFX
  id `com.venostavisuals.ExposureFalseColor`, OpenFX parameter prefix `ef`, disk IDs append-only.
- It is a **viewing aid**: the effect description, README and preset names make clear it must be
  disabled (or the layer muted) before export; there is no reliable way for an effect to know it
  is being exported. Bit-exact bypass when off.
- Deterministic, resolution independent (stripe width and legend size relative to frame height),
  alpha unchanged, transparent pixels untouched, no NaN/Inf.
- CPU reference with tests (band boundaries, monotonic stops, zebra coverage, split geometry,
  clipping, bypass); GPU (Metal / CUDA) through the shared engine, checked against the CPU
  reference like the other plugins.
- Honesty: never claim host-tested behaviour that was not tested in a host.
