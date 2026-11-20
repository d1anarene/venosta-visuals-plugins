# Conductor — progress

## 2026-11-19 — planned

- Idea and product shape agreed with the owner: a Premiere UXP panel turning audio (and later
  markers/beat grid) into keyframes on exactly one parameter per Conductor; Premiere only.
- Desk research in the `@adobe/premierepro` 27.0 typings (from the Track Range Exporter):
  keyframe creation/removal and interpolation modes on any `ComponentParam` exist; clip speed is
  readable only (`getSpeed`), no documented setter; media file paths are available
  (`getMediaFilePath`). Nothing verified in a host yet.
- Next: phase C0 feasibility spike (see AGENTS.md §5).

## 2026-11-19 — C0 test panel built (not yet run in Premiere)

- `spike/`: UXP research panel "Conductor Test (research)" (TypeScript, esbuild, .ccx built in
  CI as artifact `research-conductor-test-panel`). Steps: inspect selected clips (components,
  keyframable parameters, speed), write 1/25/250/2500 pulse keyframes onto a chosen parameter in
  one undo step (time base in point / clip start / zero; linear, bezier or hold), remove them,
  try time remapping (100/300/100 %), read an audio clip's media file and decode WAV (RMS
  envelope), round-trip a file in the plugin data folder. Everything is logged for the owner.
- Unit tests (vitest): WAV reader (16/24/32-bit PCM, 32-bit float, stereo), RMS level of a known
  sine, pulse generator. Typecheck and build pass locally. Nothing verified in a host yet.

## 2026-11-20 — C0 host test, round 1 (owner, Premiere Pro 27.0, macOS, German UI)

Host-tested:
- Selection, component chains and parameters are readable (video: Opacity, Motion; audio:
  Volume, Channel Volume). Display names are localised (match names are not: `AE.ADBE Motion`).
- Writing a keyframe onto Motion › Scale works in one transaction (12 ms), interpolation change
  works (bezier, 1 ms), value read back exactly (120 at 1 s). Clip had in = start = 0, so the
  time base (source vs sequence time) is not settled yet.
- Media files of audio clips (MP3, MP4) are readable through `localFileSystem` (16 MB in 14 ms).
- Frame rate from `getTimebase()`: 10594584000 ticks = 23.976 fps.

Observed, needs follow-up:
- The video clip's component chain lists only Opacity and Motion: **Time Remapping is not in
  it**, so it is probably not reachable from UXP (step 3 not run yet: no video clip selected).
- Not yet tested: 25/250/2500 keyframes (speed, undo), trimmed clip (time base), storage.
- Compressed audio needs decoding: options are a WebAssembly decoder (if UXP runs WebAssembly;
  test added) or letting Premiere export the audio as WAV (`EncoderManager.exportSequence`,
  `ExportType.IMMEDIATELY`, with a WAV preset), which would also cover every format Premiere reads.
- Panel was not scrollable: fixed in 0.0.2.
