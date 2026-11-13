# Exposure & False Color (Premiere Pro + DaVinci Resolve)

A **viewing aid** for judging exposure while you grade: put it on an adjustment layer at the top
of the timeline, look, then switch it off. **Turn it off (or mute the layer) before you export** —
an effect cannot know whether it is being exported.

| Tool | What you see |
| --- | --- |
| **False Color** | Every pixel coloured by its exposure in **stops from middle grey** (18 % linear, adjustable), independent of the working space |
| **Zebras** | Diagonal white stripes where the signal is at or above a level (default 95 %); optional second zebra (grey, other direction) for a range, e.g. 70–80 % for skin |
| **Clipping** | Red where a channel reaches 100 %, blue where one reaches 0 % |
| **Split View** | The overlays on one side of a movable divider, the real image on the other |
| **Legend** | A small colour bar of the false-colour scale at the bottom of the frame |

False-colour scale (stops relative to middle grey; our own layout):

| Stops | Colour | Meaning |
| --- | --- | --- |
| below −6.5 | purple | crushed blacks |
| −6.5 … −4.5 | blue | deep shadows |
| −4.5 … −2.5 | teal | shadows |
| −2.5 … −0.3 | grey (the pixel's brightness) | lower mids |
| −0.3 … +0.3 | **green** | middle grey |
| +0.3 … +0.8 | grey | upper mids |
| +0.8 … +1.4 | **pink** | typical skin highlight |
| +1.4 … white − 0.45 | grey | brights |
| just below diffuse white | yellow | near white |
| diffuse white and above | **red** | clipping in SDR / HDR highlights |

Presets: *False Color, Zebras 95%, Skin Zebras 70-80%, Clipping Check, Split False Color*.

> **Status: host-tested on macOS and Windows — user-tested with v1.0.17 in Premiere Pro 2027 and
> DaVinci Resolve 21 on Windows (NVIDIA GPU) and in Premiere Pro 2027 and DaVinci Resolve 21.2
> on an Apple Silicon Mac.** Installation, the effect in the effects list, rendering, controls
> and presets work (reported by the owner, 2026-11-13). Not yet tested: exports, Windows on Arm
> and Linux; on Windows the GPU renderer (CUDA) was not confirmed separately.
> CPU reference with tests (band boundaries, zebra coverage and direction, split
> geometry, clipping, legend, bypass, alpha) and the GPU pass checked against it; Metal (macOS) and
> CUDA (Windows) rendering. Built and installer-tested in CI for Premiere Pro and DaVinci Resolve on
> the shared engine. Export matching, long timeline stability, HDR workflows and performance measurements remain
> unverified. Free and without warranty — see the [root README](../../README.md#️-no-guarantee).

## Install

From the [Releases page](https://github.com/d1anarene/venosta-visuals-plugins/releases):
`ExposureFalseColor-Premiere-mac-<version>.pkg`, `ExposureFalseColor-Premiere-Windows-Setup-<version>.exe`,
`ExposureFalseColor-Resolve-mac-<version>.pkg`, `ExposureFalseColor-Resolve-Windows-Setup-<version>.exe`,
`ExposureFalseColor-Resolve-linux-<version>.tar.gz`. Premiere: Effects → Video Effects → Venosta
Visuals. Resolve: Effects library → OpenFX → Venosta Visuals. Installers are not code-signed.

## Develop

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The whole effect is one per-pixel function (`src/core/ef_kernels.h`) shared verbatim by the CPU,
CUDA and Metal paths. Premiere: `-DEF_BUILD_PLUGIN=ON` (+ Adobe SDKs); Resolve: `-DEF_BUILD_OFX=ON`.
Spec: [AGENTS.md](AGENTS.md); state: [PROGRESS.md](PROGRESS.md).
