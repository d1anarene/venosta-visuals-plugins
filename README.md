# Venosta Visuals Plugins

**Free plugins for Adobe Premiere Pro, made by a colorist for colorists and editors.**

This repository collects small, focused tools that remove repetitive work from grading and
delivery. More plugins (and possibly other Adobe apps) will follow.

> 🎬 **Check out my portfolio at [venostavisuals.com](https://venostavisuals.com)** — ready-to-install
> downloads of these plugins will be provided there as well.
>
> 📦 Test builds are published on the [Releases page](https://github.com/d1anarene/venosta-visuals-plugins/releases) after every update.

---

## Plugins

| Plugin | What it does | Status |
| --- | --- | --- |
| [Track Range Exporter](plugins/track-range-exporter/README.md) | Pick a guide track (e.g. V1). Every clip on it becomes its own export job in Adobe Media Encoder, rendering the **full graded sequence** (all tracks, adjustment layers, Lumetri, audio) for exactly that clip's time range. | Early version — **works in Premiere Pro 27 on macOS** (user-tested); frame-exact checks and Windows still open |
| [Film Effects](plugins/film-effects/README.md) | Native video effects with **Bloom**, **Halation** and **Film Grain** — combined in one effect or as three standalone effects — designed to sit after Lumetri or on an adjustment layer. | In development — image core unit-tested; Metal GPU code verified on a Mac GPU in CI; CUDA code compiled and verified in CPU emulation (not yet run on an NVIDIA GPU); **native plugin not yet built/tested inside Premiere** |

Each plugin folder has its own README with installation steps, usage and known limitations.

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
- Windows and macOS: Track Range Exporter is a UXP panel (both systems); Film Effects builds on
  both, with GPU acceleration via CUDA (Windows, NVIDIA) and Metal (macOS)

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

GitHub Actions (`.github/workflows/tests.yml`) runs these on every push (Linux, macOS, Windows),
plus jobs that compile and check the Film Effects GPU code: Metal (run on a Mac GPU and compared
with the CPU reference) and CUDA (compiled with nvcc; pass sequence verified in CPU emulation).

Development happens on the `dev` branch; every merge into `main` publishes a release
(`.github/workflows/release.yml`, version from the `VERSION` file).

Each plugin has an `AGENTS.md` (specification), `PROGRESS.md` (current state) and docs for
its technical decisions. Repository-wide rules are in [AGENTS.md](AGENTS.md).

## License

[MIT](LICENSE) — free to use, modify and share, with no warranty.
