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
