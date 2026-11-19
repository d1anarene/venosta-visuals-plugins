# Conductor — progress

## 2026-11-19 — planned

- Idea and product shape agreed with the owner: a Premiere UXP panel turning audio (and later
  markers/beat grid) into keyframes on exactly one parameter per Conductor; Premiere only.
- Desk research in the `@adobe/premierepro` 27.0 typings (from the Track Range Exporter):
  keyframe creation/removal and interpolation modes on any `ComponentParam` exist; clip speed is
  readable only (`getSpeed`), no documented setter; media file paths are available
  (`getMediaFilePath`). Nothing verified in a host yet.
- Next: phase C0 feasibility spike (see AGENTS.md §5).
