# Lens & Diffusion — specification for coding agents

> Plugin 3 of the Venosta Visuals collection (see the repository's `ROADMAP.md`). Built on the
> shared engine (`shared/engine`): one effect for **Premiere Pro** and **DaVinci Resolve (OpenFX)**,
> CPU reference + Metal (macOS) + CUDA (Windows), like Film Effects. Repository rules: `AGENTS.md`
> at the repository root (honest verification, README updates, no Adobe SDK files).

## Mission

Optical finishing tools a colourist otherwise buys as expensive filter packs, made from general
optics and image-processing principles (no proprietary code, names, presets or looks copied from
any product):

1. **Diffusion** — the look of a diffusion / mist filter in front of the lens: a fraction of the
   light is scattered into a wide, soft point-spread function. Range from "mist" (all tones, lifts
   shadows a little, lowers contrast) to "black mist" (mostly highlights, blacks stay deep).
2. **Vignette** — natural optical light fall-off (cos⁴-like) or a styled vignette; darken or lighten.
3. **Chromatic Aberration** — lateral colour fringing that grows towards the frame edges (red and
   blue images scaled slightly differently from green).
4. **Anamorphic Streaks** — horizontal light streaks from bright highlights (tinted, default
   blue-ish), like flares from cylindrical lens elements.

One effect named **Lens & Diffusion**, one panel, each section with its own on/off switch, plus a
few presets. Typically used after the grade on a clip or on an adjustment layer.

## Requirements

- **Identifiers (never change after release):** Premiere match name `VVIS LensDiffusion`, OpenFX id
  `com.venostavisuals.LensDiffusion`, OpenFX parameter prefix `ld`, parameter disk IDs append-only.
- **Light model:** scattering, streaks and vignette work in linear light (same working spaces as
  Film Effects: Rec.709 / gamma 2.4, sRGB, Linear); no clipping of HDR values; alpha unchanged;
  transparent pixels untouched; Global Mix against the source; bit-exact bypass when off.
- **Energy:** diffusion redistributes light (`out = (1 - s)·in + s·(in ⊛ PSF)`, with highlight
  weighting for the black-mist end) instead of only adding glow; document deviations.
- **Resolution independence:** sizes in % of frame height (or pixels of a 1080-line frame), so HD,
  4K and previews look the same.
- **Determinism:** no randomness needed; if any is added, it must follow the engine's hashing rules.
- **Processing order (to be confirmed in ARCHITECTURE.md):** input → linearise → chromatic
  aberration (geometric resampling) → diffusion → anamorphic streaks → vignette → encode → mix.
- **GPU:** CPU reference first, then Metal and CUDA through the shared engine; GPU results must
  match the CPU reference within documented tolerances (tests like Film Effects').
- **Tests:** bypass exactness, energy behaviour of diffusion, monotonicity of controls, CA zero at
  the frame centre and growing radially, streaks horizontal and only from highlights, vignette
  symmetric and centred, no NaN/Inf, odd sizes, alpha.
- **Honesty:** never claim host-tested behaviour that was not tested in the host.
