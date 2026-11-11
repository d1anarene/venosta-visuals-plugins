# Technical decisions — Track Range Exporter

## Feasibility assessment (Phase 0)

Sources actually read (Nov 2026). `developer.adobe.com` was blocked by the build machine's
network, so the **source repositories of the same docs** on GitHub and Adobe's npm type package
were used instead:

| Source | What it confirmed |
| --- | --- |
| npm `@adobe/premierepro@27.0.0` (`src/premierepro.d.ts`) | Exact Premiere UXP signatures used by this plugin (below) |
| github.com/AdobeDocs/uxp-premiere-pro-samples, `sample-panels/premiere-api` | Build setup, `lockedAccess` + `executeTransaction` pattern for `createSetInPointAction`/`createSetOutPointAction`, `exportSequence`, encoder events |
| github.com/AdobeDocs/uxp-media-encoder (source of developer.adobe.com/media-encoder/uxp) | `RenderQueue.enqueueFile`, `getProjectItemGUIDs`, `RenderOptions.setSequenceGUID`, `setCustomInAndOutPoints`, `RenderJob.getStatus` (all **"public beta", since 27.0**) |
| github.com/AdobeDocs/uxp-media-encoder-plugin-samples, `renderOptions.ts` | RenderOptions is passed as the **4th argument** of `renderFile`/`enqueueFile` (the docs' parameter table omits it) |

### Verified signatures (from the official type declarations)

- `Project.getActiveProject(): Promise<Project>`; `project.getActiveSequence()`, `project.path`, `project.save()`, `project.lockedAccess(cb)`, `project.executeTransaction(cb, undoString): boolean`
- `sequence.getVideoTrackCount()`, `getVideoTrack(i)`, `getInPoint()`, `getOutPoint()`, `getZeroPoint()`, `getSettings()`, `createSetInPointAction(TickTime)`, `createSetOutPointAction(TickTime)`, `guid: Guid`
- `SequenceSettings.getVideoFrameRate(): FrameRate` with `ticksPerFrame: number`
- `videoTrack.getTrackItems(Constants.TrackItemType.CLIP | TRANSITION, includeEmpty)`, `isMuted()`, `name`
- `VideoClipTrackItem.getStartTime()/getEndTime()` — documented as **sequence time** ("relative to the sequence start time"), `getName()`, `isDisabled()`, `isAdjustmentLayer()`
- `TickTime.ticks: string`, `TickTime.createWithTicks(string)`
- `EncoderManager.getManager().exportSequence(sequence, ExportType.QUEUE_TO_AME, outputFile?, presetFile?, exportFull?) : Promise<boolean>`; `isAMEInstalled`; `launchEncoder()`; `startBatchEncode()`; `EncoderManager.getExportFileExtension(sequence, presetPath)`

### Key finding

Premiere's own `exportSequence` has **no per-job range parameter** — only `exportFull` (whole
sequence) vs. the sequence's In/Out. `encodeFile` has in/out parameters but encodes a *media file*,
which would violate the "full sequence composition" requirement, so it is not used.

Per-job ranges without touching the timeline exist only on the **AME side**
(`RenderOptions.setCustomInAndOutPoints`), which runs inside Media Encoder, reads a **saved
`.prproj` from disk**, and is a public beta. UXP plugins in different apps cannot call each other.

## Chosen architecture: two queue routes behind one interface

Both routes share the same scanner, plan, naming and validation (`src/domain`), and both render
the **active sequence** through Adobe's pipeline (never source files, never a soloed track).

### Route A — "Premiere direct" (default, one click)

For each job, strictly one at a time: set sequence In/Out in one undoable transaction → read back
and verify exact ticks → `exportSequence(seq, QUEUE_TO_AME, file, preset, exportFull=false)` →
await → next job. In a `finally` block the original In/Out is restored and read back; a failed
restore is shown as a prominent error with the original tick values. Before every job the timeline
is re-scanned and compared to the plan fingerprint; any change stops the run.

Why it should work: Premiere hands AME a serialized copy of the sequence when a job is queued,
which is why (in normal manual use) changing the timeline after "Queue" does not change queued
jobs. **This is the assumption that the spike test (below) must confirm for the UXP API.** If the
test shows two jobs with the same range, Route A must be disabled and Route B used.

Safety properties (unit-tested with a fake host): serial only; refuses to queue when the read-back
differs; stops after the first failure; stops on cancel and on timeline change; always restores.

### Route B — "Media Encoder companion" (zero timeline mutation)

Premiere panel saves the project, then writes a JSON job file (`src/export/handoffManifest.ts`)
into the output folder. The **Track Range Exporter – AME** panel loads it and, per job:
`RenderOptions` + `setSequenceGUID(guid)` + `setCustomInAndOutPoints(start, out)` →
`RenderQueue.enqueueFile(prproj, preset, output, options)`; `result === 0` → "queued" with AME's
job id; status is polled with `getJob(id).getStatus()`.

Trade-offs: needs AME 27.0+ with UXP (beta) and a second panel; renders the **saved** project, so
unsaved changes are not included (the panel saves before writing the file); `getProjectItemGUIDs`
only lists root-level sequences, so a sequence inside a bin produces a warning, not a block.

## Time representation

- All ranges are integer ticks (`bigint` internally, decimal strings at boundaries). Seconds are
  display-only.
- Ticks per frame comes from the sequence (`FrameRate.ticksPerFrame`). The table of common rates in
  `frameMath.ts` is only used by tests; nothing assumes a universal constant.
- Internal convention: **[start, end)**. Clip `getEndTime()` is the first tick after the clip.
- At the Adobe boundary the out point is sent either as `end` ("exclusive", default) or
  `end − 1 frame` ("inclusive"), selectable under *Advanced*. Which one Premiere/AME expect is
  **unverified**; the spike test measures it (an export with one extra frame means "inclusive").
- Clip boundaries that are not frame-aligned are flagged and sent unchanged — never shifted.

## Transitions

A transition overlapping a guide clip is flagged (`TRANSITION_AT_BOUNDARY`). The export range is
always exactly the clip's own `[start, end)` on the timeline. With a centered dissolve this means
each export contains its half of the dissolve, including frames of the neighbouring shot.

## Not used, on purpose

- QE DOM / ExtendScript / GUI automation: unsupported or deprecated.
- Duplicating sequences per job: pollutes the project and is unnecessary with either route.
- ffmpeg or any re-encoding outside Adobe: cannot reproduce Premiere's compositor/Lumetri.
- Muting/soloing tracks: forbidden by the spec; the timeline is rendered as-is.
