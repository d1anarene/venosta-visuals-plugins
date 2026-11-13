#!/usr/bin/env bash
# Writes the release notes (Markdown) for tag $1 from the files in directory $2 to stdout.
# One section per plugin with a download table: rows = program, columns = operating system.
# A cell links only files that exist in $2, so a missing build shows "—" instead of a dead link.
set -euo pipefail
tag="$1"
dir="$2"
repo="${GITHUB_REPOSITORY:-d1anarene/venosta-visuals-plugins}"

# link <file name without version> <extension> <label> [note]
link() {
  local f="$1-${tag}.$2"
  if [ -e "$dir/$f" ]; then
    printf '[%s](https://github.com/%s/releases/download/%s/%s)' "$3" "$repo" "$tag" "$f"
    [ -n "${4:-}" ] && printf '<br><sub>%s</sub>' "$4"
  else
    printf '—'
  fi
}

# effect_table <file prefix> <Premiere mac note>
effect_table() {
  local p="$1"
  echo "| Program | Windows | macOS | Linux |"
  echo "| --- | --- | --- | --- |"
  echo "| **Premiere Pro** | $(link "$p-Premiere-Windows-Setup" exe "Setup.exe" "x64 (NVIDIA CUDA / CPU) + Windows on Arm (CPU), Premiere 26+ on Arm") | $(link "$p-Premiere-mac" pkg "Installer .pkg" "$2") | — |"
  echo "| **DaVinci Resolve** | $(link "$p-Resolve-Windows-Setup" exe "Setup.exe" "x64 (NVIDIA CUDA / CPU) + Windows on Arm (CPU)") | $(link "$p-Resolve-mac" pkg "Installer .pkg" "Apple Silicon + Intel (Metal), macOS 12+") | $(link "$p-Resolve-linux" tar.gz ".tar.gz" "x86-64 (CPU), extract into /usr/OFX/Plugins") |"
}

cat <<NOTES
Free plugins for Adobe Premiere Pro, Media Encoder and DaVinci Resolve by Venosta Visuals — https://venostavisuals.com
Provided "as is", without any warranty. Keep backups of your projects.

**Installing:** quit the program, run the installer, start the program again. One Windows Setup covers normal PCs and Windows on Arm (it installs the right build itself). Installers are not code-signed: on a Mac allow them in System Settings → Privacy & Security; on Windows click "More info" → "Run anyway". Uninstall on Windows via Settings → Apps.
Effects appear under **Video Effects → Venosta Visuals** in Premiere and under **OpenFX → Venosta Visuals** in Resolve.

### Film Effects
Free film finishing effects: Bloom, Halation, Film Grain and optional Film Gate (weave, flicker, dust, scratches, gate edge) in one panel, to finish your existing grade. The former standalone Film Bloom / Halation / Grain are part of it and are removed on upgrade.

$(effect_table FilmEffects "Apple Silicon + Intel (Metal), macOS 26+")

Status: user-tested (v1.0.17) in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on macOS (Apple Silicon): installs, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested. CPU tests and Metal-vs-CPU comparisons pass in CI.

### Lens & Diffusion
Diffusion filter (mist to black-mist look), vignette, chromatic aberration, anamorphic streaks, presets.

$(effect_table LensDiffusion "Apple Silicon + Intel (Metal), macOS 13+")

Status: user-tested (v1.0.17) in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on macOS (Apple Silicon): installs, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested. CPU tests and Metal-vs-CPU comparisons pass in CI.

### Exposure & False Color (viewing aid)
False color in stops from middle grey (with legend), zebras, clipping, split view. **Turn it off before exporting** — an effect cannot know it is being exported.

$(effect_table ExposureFalseColor "Apple Silicon + Intel (Metal), macOS 13+")

Status: user-tested (v1.0.17) in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on macOS (Apple Silicon): installs, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested. CPU tests and Metal-vs-CPU comparisons pass in CI.

### Retro
Analog video look: color bleed (soft luma, ringing, smeared and shifted chroma), tape (line jitter, tracking band, dropouts, head switching), noise, CRT (scanlines, aperture grille / shadow mask, curvature). Presets: Home Video Tape, Worn Tape, Broadcast 80s, CRT Monitor, Subtle Analog.

$(effect_table Retro "Apple Silicon + Intel (Metal), macOS 13+")

Status: user-tested (v1.0.17) in Premiere Pro 2027 and DaVinci Resolve 21 on Windows (NVIDIA) and in Premiere Pro 2027 and DaVinci Resolve 21.2 on macOS (Apple Silicon): installs, renders, controls and presets work. Exports, Windows on Arm and Linux not yet tested. CPU tests and Metal-vs-CPU comparisons pass in CI.

### Track Range Exporter
Every clip on a guide track becomes its own Media Encoder job, rendering the full graded sequence for exactly that clip's range. Double-click a .ccx to install it with Creative Cloud Desktop (uninstall: Creative Cloud Desktop → Plugins → Manage Plugins); in Premiere open Window → UXP Plugins → Track Range Exporter.

| Program | Windows | macOS | Linux |
| --- | --- | --- | --- |
| **Premiere Pro** | $(link TrackRangeExporter-Premiere ccx "Panel .ccx" "same file for Windows and macOS") | $(link TrackRangeExporter-Premiere ccx "Panel .ccx" "same file for Windows and macOS") | — |
| **Media Encoder** (optional, AME 27+) | $(link TrackRangeExporter-MediaEncoder ccx "Companion .ccx" "only for the \"no timeline change\" queue method") | $(link TrackRangeExporter-MediaEncoder ccx "Companion .ccx" "only for the \"no timeline change\" queue method") | — |

Status: works in Premiere Pro 27 on macOS (user-tested); Windows and frame-exact checks still open.

---
GPU rendering: Metal on macOS, NVIDIA CUDA on Windows (needs an NVIDIA driver, not the CUDA Toolkit; includes GTX 1080 / Pascal code), CPU otherwise — one download per system. No Adobe SDK files are included. Status of each plugin: README and each plugin's PROGRESS.md.
NOTES
