# Film Effects (Premiere Pro)

Native Premiere Pro video effects with three film-finishing tools — **Bloom**, **Halation** and
**Film Grain** — made to sit after Lumetri Color on a clip, or on an adjustment layer above your
graded timeline.

Four effects are installed under **Venosta Visuals**:
- **Film Effects** — all three tools in one effect, each with its own on/off switch, plus presets
- **Film Bloom**, **Film Halation**, **Film Grain** — each tool on its own, when you only need one

They share the same processing code, so Film Grain looks exactly like Film Effects with only
Grain switched on.

> ⚠️ **Status: in development — not yet tested inside Premiere Pro.** The image processing core is
> unit-tested and the Metal GPU code is verified against it on a Mac GPU in CI; the native plugin
> itself still needs its first build with the Adobe SDK (see [PROGRESS.md](PROGRESS.md)). Provided without any guarantee —
> see the [root README](../../README.md#️-no-guarantee).

Downloads will also be available at [venostavisuals.com](https://venostavisuals.com).

![before/after](docs/images/night_street_subtle_film.png)
<sub>CPU reference render of a synthetic test scene ("Subtle Film" preset) — not a Premiere screenshot.</sub>

## What it does

| Tool | What you get | Main controls |
| --- | --- | --- |
| **Bloom** | Soft, multi-scale glow from highlights that keeps each light's colour | Intensity, Threshold, Soft Knee, Radius (% of frame height) |
| **Halation** | Warm red/orange fringe just outside bright edges (windows, lamps, neon) — not a red wash | Intensity, Threshold, Radius, Warmth |
| **Film Grain** | Organic, clumped grain that follows the image's tones, changes every frame and is identical every time you render the same frame | Amount, Size, Roughness, Shadow/Midtone/Highlight response, Luma/Color, Seed, Animate |

Global: Master Enable, Global Mix, Working Space (Rec.709 gamma 2.4 / sRGB / Linear), Processing
Quality. Presets: *Subtle Film, Soft Highlight Glow, Night Lights, Fine 35mm Grain, Coarse 16mm
Grain, Halation Only, Off* — choosing one fills in the normal parameters, which you can then tweak
and keyframe.

## Install

**Platforms:** macOS (CPU + Metal GPU acceleration) and Windows (CPU rendering for now; GPU
acceleration via CUDA is planned). Build and install steps (need the free Adobe SDKs):
[docs/INSTALLATION.md](docs/INSTALLATION.md).
After installing and restarting Premiere: **Effects → Video Effects → Venosta Visuals**.

## Use

1. Grade your clips with Lumetri as usual.
2. Add an **Adjustment Layer** above the graded clips (or apply the effect after Lumetri on a clip).
3. Drag **Film Effects** onto it, pick a preset, then adjust Bloom / Halation / Grain
   (or use Film Bloom / Film Halation / Film Grain on their own).
4. Effects above it in the stack see the result; effects below it feed into it. On an adjustment
   layer it processes everything visible beneath it.

Tips: keep Working Space on *Rec.709 / Gamma 2.4* for normal SDR timelines; use *Linear* only if
your sequence works in linear light. Grain size is defined relative to a 1080-line frame, and the
bloom/halation radius relative to frame height, so the look stays the same in HD, 4K and in
reduced-resolution playback.

## How it works / docs

[ARCHITECTURE](docs/ARCHITECTURE.md) · [FEASIBILITY](docs/FEASIBILITY.md) ·
[IMAGE_QUALITY](docs/IMAGE_QUALITY.md) · [PERFORMANCE](docs/PERFORMANCE.md) ·
[LICENSING](docs/LICENSING.md) · [PROGRESS](PROGRESS.md)

## Develop

```bash
./scripts/run-core-tests.sh          # build + run the core tests (Linux/macOS/Windows)
./build/fe_render --scene night-street --preset "Night Lights" --before in.ppm --out out.ppm
./scripts/make-fixtures.sh           # all scenes x presets
```
