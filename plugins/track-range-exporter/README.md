# Track Range Exporter (Premiere Pro)

Export **one file per clip** from a finished, graded timeline — without setting In/Out by hand.

Pick a guide track (usually **V1**). Each clip on it becomes its own Media Encoder job that renders
the **full sequence** for exactly that clip's time range: every visible track, adjustment layers,
Lumetri, titles, transitions and audio. The guide track only supplies the start and end times.

> ⚠️ **Status: in development, not yet tested inside Premiere Pro.** The logic is unit-tested, but
> the Adobe integration still needs the host test in [docs/HOST_TEST_PLAN.md](docs/HOST_TEST_PLAN.md).
> Check your first exports carefully. Provided without any guarantee — see the
> [root README](../../README.md#️-no-guarantee).

Downloads will also be available at [venostavisuals.com](https://venostavisuals.com).

## Install (developer build, ~2 minutes)

Requirements: Premiere Pro 25.6 or newer, Adobe Media Encoder, Node.js 18+, and the free
[UXP Developer Tool](https://developer.adobe.com/premiere-pro/uxp/) from Creative Cloud.

```bash
cd plugins/track-range-exporter
npm install
npm run build
```

1. Open **UXP Developer Tool** → *Add Plugin* → choose `dist/premiere-panel/manifest.json`.
2. Start Premiere Pro, then click **Load** in UXP Developer Tool.
3. In Premiere: **Window → UXP Plugins → Track Range Exporter** (dock it wherever you like).

Optional, for the no-timeline-change method: load `dist/ame-companion/manifest.json` the same way
with **Media Encoder 27.0+** as the target app.

## Use

1. Open your graded sequence. The panel shows its name (↻ refreshes it).
2. Choose the **guide track** (V1 by default) and press **Scan Track**.
3. Review the list: one row per clip with IN, OUT (exclusive), frame count and file name.
   Untick clips you don't want, rename files if needed.
4. **Choose Folder…** and **Choose AME Preset…** (an `.epr` file — e.g. exported from Media
   Encoder's Preset Browser). The file extension comes from the preset.
5. Press **Add Selected to AME Queue**, then render in Media Encoder (or press *Start AME Queue*).

Files are named `001_A.mov`, `002_B.mov`, … Existing files are never overwritten — a number is
added instead (`001_A (2).mov`).

### Two queue methods

| | Premiere direct (default) | Media Encoder companion |
| --- | --- | --- |
| Clicks | One, inside Premiere | Write job file in Premiere, load it in the AME panel |
| Timeline | In/Out changed per job, then **restored** to yours | Never touched |
| Renders | The sequence as it is when queued | The **saved** project (the panel saves first) |
| Needs | Premiere 25.6+, AME | Media Encoder 27.0+ (UXP beta) and the companion panel |

## Good to know

- Gaps on the guide track are never exported, and adjacent clips never share a frame.
- Disabled guide clips are skipped by default (*Advanced* → include them).
- A transition across a cut is flagged: each file contains its own part of the dissolve.
- If you edit the timeline after scanning, queueing is refused until you scan again.
- If every export has one frame too many, switch *Advanced → Out point* to "inclusive" (and tell me).

More detail: [TECHNICAL_DECISIONS.md](TECHNICAL_DECISIONS.md) ·
[KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) · [PROGRESS.md](PROGRESS.md)

## Troubleshooting

- **"No active sequence"**: click into the Timeline panel so a sequence is active, then ↻.
- **"Media Encoder was not found"**: install AME of the same version family as Premiere.
- **"Could not read the output format from the preset"**: the `.epr` was moved or is invalid.
- **Restore warning**: if the panel says it could not restore your In/Out, use *Edit → Undo*
  ("Track Range Exporter: …" entries) or set them again manually.

## Develop

```bash
npm test          # unit tests
npm run typecheck
npm run build
```
