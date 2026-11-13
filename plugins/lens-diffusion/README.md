# Lens & Diffusion (Premiere Pro + DaVinci Resolve)

One video effect, **Lens & Diffusion**, with four optical finishing tools, each with its own on/off
switch, plus presets:

| Tool | What you get | Main controls |
| --- | --- | --- |
| **Diffusion** | The look of a diffusion / mist filter: part of the light is scattered into a soft, wide glow. From "mist" (all tones, lifts shadows a little, lowers contrast) to a black-mist style (mostly highlights, blacks stay deep). Light is redistributed, not added. | Amount, Size, Halo, Highlight Bias, Warmth |
| **Vignette** | Natural optical light fall-off (cos⁴) or a smooth styled vignette; darken or lighten | Amount, Size, Roundness, Feather, Style |
| **Chromatic Aberration** | Colour fringes that grow towards the frame edges, none in the centre — subtle to deliberately extreme | Amount (px at the corner of a 1080 frame; slider ±50, type up to ±300), Edge Bias, **Aberration Spectral Smear** (0 = classic red/blue fringe, higher = continuous rainbow smear; listed at the bottom of the panel) |
| **Anamorphic Streaks** | Horizontal light streaks from bright highlights, tinted | Intensity, Threshold, Soft Knee, Length, Color |

Global: Master Enable, Global Mix, Working Space (Rec.709 gamma 2.4 / sRGB / Linear). Presets:
*Subtle Mist, Strong Mist, Highlight Mist, Vintage Lens, Anamorphic Night, Natural Vignette, Off*.

> **Status: host-tested on macOS and Windows — user-tested with v1.0.17 in Premiere Pro 2027 and
> DaVinci Resolve 21 on Windows (NVIDIA GPU) and in Premiere Pro 2027 and DaVinci Resolve 21.2
> on an Apple Silicon Mac.** Installation, the effect in the effects list, rendering, controls
> and presets work (reported by the owner, 2026-11-13). Not yet tested: exports, Windows on Arm
> and Linux; on Windows the GPU renderer (CUDA) was not confirmed separately.
> CPU reference with tests (energy conservation, symmetry, fringes, streak direction,
> bypass exactness, alpha); GPU rendering with **Metal** (macOS) and **NVIDIA CUDA** (Windows),
> whose pass sequence is checked against the CPU reference. Built for Premiere Pro (macOS, Windows
> x64 and Windows on Arm) and DaVinci Resolve (macOS, Windows, Linux) on the shared engine; loaded
> and installer-tested in CI. Export matching, long timeline stability, HDR workflows and performance measurements
> remain unverified. Free and provided without warranty — see the [root
> README](../../README.md#️-no-guarantee).

## Install

From the [Releases page](https://github.com/d1anarene/venosta-visuals-plugins/releases):

- Premiere Pro: `LensDiffusion-Premiere-mac-<version>.pkg` or `LensDiffusion-Premiere-Windows-Setup-<version>.exe`
- DaVinci Resolve: `LensDiffusion-Resolve-mac-<version>.pkg`, `LensDiffusion-Resolve-Windows-Setup-<version>.exe`
  or `LensDiffusion-Resolve-linux-<version>.tar.gz` (extract into `/usr/OFX/Plugins`)

Restart the application. Premiere: **Effects → Video Effects → Venosta Visuals → Lens & Diffusion**.
Resolve: Effects library → **OpenFX → Venosta Visuals**. The installers are not code-signed (allow
them in System Settings → Privacy & Security on the Mac, or confirm the Windows warning).

## Develop

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build   # core tests (any OS)
./build/ld_render --scene night-street --preset "Vintage Lens" --out out.ppm
```

Premiere: `-DLD_BUILD_PLUGIN=ON -DAE_SDK_DIR=... -DPR_SDK_DIR=...` (Adobe SDKs, not in this
repository). Resolve: `-DLD_BUILD_OFX=ON`. Everything that is not specific to this effect comes
from the shared engine (`shared/engine`). Specification: [AGENTS.md](AGENTS.md); state:
[PROGRESS.md](PROGRESS.md).
