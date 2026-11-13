# Retro (Premiere Pro + DaVinci Resolve)

An analog video look in one effect, with sections you can switch on and off:

| Section | What it does |
| --- | --- |
| **Color Bleed** | The picture is treated like a composite / tape signal (Y'UV): soft luma with edge **ringing**, colour smeared sideways and **shifted**, saturation |
| **Tape** | Per-line **jitter**, a **tracking** band that rolls through the frame, **dropouts** (short bright streaks), **head switching** at the bottom edge |
| **Noise** | Luma and chroma noise ("snow"), new every frame |
| **CRT** | **Scanlines**, aperture-grille or shadow-mask RGB pattern, **screen curvature** with dark corners |

Presets: *Home Video Tape, Worn Tape, Broadcast 80s, CRT Monitor, Subtle Analog, Off* — choose
one and adjust anything afterwards. **Mix** blends with the original; **Seed** and **Animate**
control the randomness (the same frame always renders the same, in any order).

Sizes are given in pixels of a 1080-line frame and the scanline count per frame height, so HD, 4K
and reduced-resolution previews look alike.

> **Status: host-tested on macOS — user-tested in Premiere Pro 27 and DaVinci Resolve on Apple
> Silicon.** CPU reference with tests (signal maths, bleed direction, ringing, jitter, noise
> statistics, scanline anti-aliasing, mask energy, curvature, determinism, bypass, alpha); the GPU
> passes are checked against it. Metal (macOS) and CUDA (Windows) rendering. Built and installer-
> tested in CI for Premiere Pro and DaVinci Resolve on the shared engine. Windows (including Windows
> on Arm) and Linux host operation remain unverified. Export matching, long timeline stability, HDR
> workflows and performance measurements remain unverified. Free and without warranty — see the
> [root README](../../README.md#️-no-guarantee).

## Install

From the [Releases page](https://github.com/d1anarene/venosta-visuals-plugins/releases):
`Retro-Premiere-mac-<version>.pkg`, `Retro-Premiere-Windows-Setup-<version>.exe`,
`Retro-Resolve-mac-<version>.pkg`, `Retro-Resolve-Windows-Setup-<version>.exe`,
`Retro-Resolve-linux-<version>.tar.gz`. Premiere: Effects → Video Effects → Venosta Visuals.
Resolve: Effects library → OpenFX → Venosta Visuals. Installers are not code-signed.

## Tips

- For a real "tape" feel, also lower the resolution of the source a little (e.g. scale to 50 % and
  back) before Retro, and keep **Mix** at 100 %.
- CRT mask stripes are one output pixel wide at 1080 lines and get wider at 4K; for small
  previews or very small exports reduce **Mask Strength** to avoid moiré from the viewer scaling.

## Develop

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Per-pixel maths shared by CPU, CUDA and Metal: `src/core/rt_kernels.h`; GPU pass order:
`src/gpu/common/rt_gpu_pipeline.h` (prepare Y'UV → three horizontal blurs on the engine pyramid →
composite). Premiere: `-DRT_BUILD_PLUGIN=ON` (+ Adobe SDKs); Resolve: `-DRT_BUILD_OFX=ON`.
Spec: [AGENTS.md](AGENTS.md); state: [PROGRESS.md](PROGRESS.md).
