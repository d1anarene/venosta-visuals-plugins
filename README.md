# Venosta Visuals Plugins

**Free plugins for Adobe Premiere Pro, made by a colorist for colorists and editors.**

This repository collects small, focused tools that remove repetitive work from grading and
delivery. More plugins (and possibly other Adobe apps) will follow.

> 🎬 **Download here or at [venostavisuals.com](https://venostavisuals.com/en/plugins)** — ready-to-install
>
> 📦 Test builds are published on the [Releases page](https://github.com/d1anarene/venosta-visuals-plugins/releases) after every update.

---

## Plugins

| Plugin | What it does | Status |
| --- | --- | --- |
| [Track Range Exporter](plugins/track-range-exporter/README.md) | Pick a guide track (e.g. V1). Every clip on it becomes its own export job in Adobe Media Encoder, rendering the **full graded sequence** (all tracks, adjustment layers, Lumetri, audio) for exactly that clip's time range. | Early version — **works in Premiere Pro 27 on macOS** (user-tested); frame-exact checks and Windows still open |
| [Film Effects](plugins/film-effects/README.md) | Free film finishing effects with **Bloom**, **Halation**, **Film Grain** and optional **Film Gate** (weave, flicker, dust, scratches, gate edge) in one panel — designed to sit after Lumetri or on an adjustment layer. | **Host-tested on macOS and Windows** — user-tested with v1.0.17 in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA GPU) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on an Apple Silicon Mac: installs, appears, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested |
| [Lens & Diffusion](plugins/lens-diffusion/README.md) | Optical finishing in one effect: **diffusion filter** (mist to black-mist look), **vignette**, **chromatic aberration** and **anamorphic streaks**, for Premiere Pro and DaVinci Resolve. | **Host-tested on macOS and Windows** — user-tested with v1.0.17 in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA GPU) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on an Apple Silicon Mac: installs, appears, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested |
| [Exposure & False Color](plugins/exposure-false-color/README.md) | Exposure **viewing aid**: false color in stops from middle grey (with legend), zebras, clipping and a split view — switch it off before export. Premiere Pro and DaVinci Resolve. | **Host-tested on macOS and Windows** — user-tested with v1.0.17 in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA GPU) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on an Apple Silicon Mac: installs, appears, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested |
| [Retro](plugins/retro/README.md) | Analog video look in one effect: **color bleed** (soft luma, ringing, smeared and shifted chroma), **tape** (line jitter, tracking band, dropouts, head switching), **noise** and **CRT** (scanlines, RGB mask, screen curvature). Premiere Pro and DaVinci Resolve. | **Host-tested on macOS and Windows** — user-tested with v1.0.17 in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA GPU) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on an Apple Silicon Mac: installs, appears, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested |
| [Denoise](plugins/denoise/README.md) | Planned detail-preserving video noise reduction, focused first on high-quality camera-log footage, with temporal + spatial filtering and Preview / Final modes. | **Research benchmark + host probes** — moving log-curve fixtures, detail/motion metrics and locally compiled research probes; no denoiser or actual host verification yet |

Each plugin folder has its own README with installation steps, usage and known limitations.
Planned next: Retro, Denoise and Retime — see
[ROADMAP.md](ROADMAP.md).

---

## ⚠️ No guarantee

These plugins are free, and are provided **"as is", without any warranty or guarantee of any
kind**. I do not guarantee that they work on your system, with your Premiere Pro version, or
for your project. Use them at your own risk, and **always keep backups of your projects and
media**. I am not liable for lost work, failed renders, missed deadlines or any other damage.
See [LICENSE](LICENSE) for the full terms.

---

## Requirements (general)

- Adobe Premiere Pro (current version; see each plugin's README for tested versions)
- Adobe Media Encoder for plugins that queue exports
- Film Effects also has an experimental DaVinci Resolve (OpenFX) build for Windows, macOS and Linux
- Film Effects Windows installers also cover Windows on Arm (CPU only, untested on Arm hardware)
- Windows and macOS: Track Range Exporter is a UXP panel (both systems); Film Effects builds on
  both; the Mac release includes Metal acceleration, while experimental Windows downloads offer CPU-only or CUDA acceleration

## For developers

```
plugins/
  track-range-exporter/   # UXP panel (TypeScript) for Premiere Pro
  film-effects/           # Native C++ effect (Premiere/After Effects SDK) + Metal GPU
```

Run the tests for each plugin:

```bash
cd plugins/track-range-exporter && npm install && npm test
```

```bash
cd plugins/film-effects && ./scripts/run-core-tests.sh
```

The plugins on the shared engine (Lens & Diffusion, Exposure & False Color, Retro) use CMake/CTest:

```bash
cd plugins/retro && cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Denoise's research tools and CPU measurement tests (not a host plugin yet):

```bash
cmake -S plugins/denoise -B build-denoise -DCMAKE_BUILD_TYPE=Release
cmake --build build-denoise --config Release --parallel
ctest --test-dir build-denoise -C Release --output-on-failure
```

Build the DaVinci Resolve (OpenFX) plug-in (any OS, no Adobe SDK needed):

```bash
cd plugins/film-effects && cmake -S . -B build-ofx -DFE_BUILD_OFX=ON && cmake --build build-ofx && ctest --test-dir build-ofx
```

Package prebuilt Mac bundles (on macOS, with Xcode):

```bash
bash shared/engine/installer/macos/package-premiere.sh /path/to/bundles /path/to/FilmEffects.pkg 1.0.0 com.venostavisuals.filmeffects.preview FilmEffects --metal --min-macos 26
```

GitHub Actions (`.github/workflows/tests.yml`) runs these on every push (Linux, macOS, Windows),
plus jobs that compile and check the Film Effects GPU code: Metal (run on a Mac GPU and compared
with the CPU reference) and CUDA (compiled with nvcc; pass sequence verified in CPU emulation).

Development happens on the `dev` branch; every merge into `main` publishes a release
(`.github/workflows/release.yml`, version from the `VERSION` file).

Each plugin has an `AGENTS.md` (specification), `PROGRESS.md` (current state) and docs for
its technical decisions. Repository-wide rules are in [AGENTS.md](AGENTS.md).

## License

[MIT](LICENSE) — free to use, modify and share, with no warranty.
