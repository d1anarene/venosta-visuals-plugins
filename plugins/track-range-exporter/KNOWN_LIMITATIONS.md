# Known limitations — Track Range Exporter

Status: **unit-tested and browser-smoke-tested against a mocked Premiere API. Not yet run inside
Premiere Pro or Media Encoder.** Everything that depends on Adobe's runtime behaviour is unverified.

## Unverified (needs the host test in docs/HOST_TEST_PLAN.md)

- **Route A (Premiere direct)** depends on Premiere capturing the sequence In/Out at the moment
  each job is queued. If it does not, all jobs would get the last range. Do not trust it for
  delivery before the spike test passes.
- **Out point convention**: whether Premiere/AME treat the out point as exclusive (clip end, our
  default) or inclusive (last frame) is unknown. Wrong setting = one extra or one missing frame per
  file. Switchable under *Advanced*.
- **Route B (AME companion)** uses the AME UXP API, which Adobe marks as public beta (27.0+).
  `enqueueFile` taking RenderOptions as a 4th argument comes from Adobe's sample code, not the
  reference table.
- Manifest `minVersion` values (Premiere 25.6, AME 27.0) are taken from the API docs, not tested.

## Actual constraints by design

- Route B renders the **saved** project file. The panel saves the project before writing the job
  file; later timeline edits are not picked up by already-written job files.
- Route A temporarily changes the sequence In/Out (restored afterwards). The changes appear in
  Premiere's undo history as "Track Range Exporter: …".
- After Route A, queued AME jobs are expected to hold their own copy of the sequence; later timeline
  edits should not change them (unverified, see above).
- `getProjectItemGUIDs` (AME) only lists root-level sequences. A sequence in a bin triggers a
  warning in the AME panel but is still requested by GUID.
- One preset for all jobs. No per-job formats, no naming templates beyond the editable names.
- Status after queueing: Route A shows "queued" only (Premiere's encoder events are not tied to
  individual jobs). Route B polls AME per job (rendering/done/failed).
- Transition detection relies on `getTrackItems(TRANSITION)`; if a Premiere build does not return
  transitions, no transition warnings are shown.
- Drop-frame timecode labels: only 29.97 DF is detected from the sequence display format. Ranges
  themselves never depend on timecode.
- The "Export audio" switch is not offered: audio follows the chosen preset.
- No Windows-specific testing; paths are handled for both platforms in code only.
