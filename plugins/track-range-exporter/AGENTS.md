# Track Range Exporter - Agent Implementation Instructions

## Mission

Build a REAL, installable Adobe Premiere Pro extension named **Track Range Exporter** (working title). Its purpose is to batch-export multiple independent video files from a single already-graded Premiere timeline. The user chooses one video track, such as V1, whose video clips define only the temporal boundaries of the exports. Each output MUST render the FULL sequence composition within that range, including relevant clips and effects on every enabled video track, adjustment layers, Lumetri grades, transitions, and enabled audio tracks.

This is a productivity tool for color grading and delivery. The user grades each source clip individually, places a global look or adjustment layer above the footage, and wants one exported file per base clip without manually setting in/out and exporting again and again.

**NON-NEGOTIABLE:** The selected track is a *range guide*, NOT a soloed or isolated export source. Do NOT export source media files, individual ProjectItems, or the selected video track alone. Do NOT flatten/render only selected track items. The deliverable must use Premiere's own sequence rendering pipeline for visual fidelity.

## User Story / Concrete Example

A sequence has:

- V1: `A.mov` at timeline [00:00:00:00, 00:00:05:00), a gap, `B.mov` at [00:00:08:00, 00:00:12:00), then `C.mov`.
- V2: titles, picture overlays, or other footage spanning one or more of those intervals.
- V3: one long adjustment layer with Lumetri and a global film look across the timeline.
- A1/A2: audio, perhaps not aligned to individual clip boundaries.

User selects V1 and clicks **Scan Track**. The extension creates 3 preview jobs, each using the precise timeline start and end of its corresponding V1 clip. On **Add to Media Encoder Queue**, it creates 3 separate AME jobs (not one concatenated movie). Job A renders the full active sequence composition from 0s to 5s; Job B renders the full composition from 8s to 12s; Job C likewise. V2, V3, sequence processing, and enabled audio contribute to all jobs according to their timeline position. No frames from timeline gaps should be exported. No extra frame at clip boundaries.

## Initial Assumptions and Targets

- Desktop Adobe Premiere Pro; primarily macOS during development, architecture should avoid needless OS lock-in.
- Modern Premiere UXP panel is preferred. TypeScript is preferred; use the Adobe UXP Developer Tool and current Adobe sample code.
- Adobe Media Encoder (AME) is the preferred delivery queue; support a straightforward 'enqueue only' workflow first. Starting AME is an additional control, not an implicit side effect.
- Target the current supported Premiere/AME versions installed in the environment; investigate minimum version requirements. Do not claim compatibility with versions not tested.
- Prioritize correct output ranges and preservation of the user's sequence above UI polish or extra features.
- Agent should actually build/run/test what the environment permits, not merely provide a proposal. If Adobe applications cannot be launched in the agent environment, complete the implementation as far as possible and document honest manual host verification steps.

## Phase 0 - Mandatory API Feasibility Spike (BEFORE building a polished UI)

Investigate official Adobe SDK documentation and official samples, and verify exact behavior against installed host versions when possible. As of November 2026 the following official APIs are documented; VERIFY signatures, semantics and version gates rather than blindly copying illustrative code:

1. Premiere UXP: `Project.getActiveProject()`, `project.getActiveSequence()`, `sequence.getVideoTrackCount()`, `sequence.getVideoTrack(index)`, `videoTrack.getTrackItems(Constants.TrackItemType.CLIP, false)`, and `VideoClipTrackItem.getStartTime()`, `.getEndTime()`, `.getName()`.
2. Premiere UXP: `EncoderManager.getManager().exportSequence(sequence, exportType, outputFile, presetFile, exportFull)` with `Constants.ExportType.QUEUE_TO_AME` and a boolean for full sequence vs in/out. Investigate how sequence in/out is captured for jobs at enqueue time. Do NOT assume that rapidly changing sequence in/out before each enqueue produces independent snapshots.
3. Premiere UXP: `sequence.getInPoint()` / `.getOutPoint()`, `sequence.createSetInPointAction(tickTime)` / `.createSetOutPointAction(tickTime)`; project mutations require the documented locked-access/transaction pattern. If testing this route, ensure in/out are restored on completion, failure and cancellation; never leave the source sequence altered. Protect against concurrent timeline changes.
4. AME UXP API (27.0+ PUBLIC BETA): `RenderQueue.enqueueFile(projectPath, presetPath, outputPath)` returns a job ID; `RenderQueue.getJob(jobId).setCustomInAndOutPoints(startTick, endTick)` allows *per-job* custom export ranges; `RenderOptions.setSequenceGUID()` provides sequence selection, and `RenderQueue.getProjectItemGUIDs()` aids selection. Examine carefully how sequence selection is associated with a queued job, the documented `enqueueFile` signature, `RenderOptions` integration and the ability of a Premiere panel to communicate with an AME panel. Some documentation is beta and may be incomplete. Do not assume APIs from separate hosts are callable in one process.
5. Identify a supported, reliable architecture for getting N jobs referencing one Premiere sequence with N independent ranges. Consider a Premiere UXP panel controlling AME via supported mechanisms, an AME-side companion UXP panel with a documented handoff, or a verified Premiere-only enqueue approach. Avoid unsupported internal APIs / QE DOM / fragile GUI automation unless a clear, opt-in compatibility fallback is justified.

Build a tiny end-to-end spike first: a sequence with 2 guide clips of DIFFERENT durations, a noticeable adjustment layer above both, then queue and render both separately. Assert job 1 and job 2 have distinct correct ranges and the grade is present. If impossible on installed versions, stop claiming end-to-end support, write a concise blocker report, and implement only the verified functionality while investigating a supported alternative.

### Official references (re-check current versions)

- https://developer.adobe.com/premiere-pro/uxp/
- https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/sequence
- https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/videotrack
- https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/videocliptrackitem
- https://developer.adobe.com/premiere-pro/uxp/ppro-reference/classes/encodermanager
- https://developer.adobe.com/premiere-pro/uxp/ppro-reference/constants/
- https://developer.adobe.com/media-encoder/uxp/media-encoder-api/render-queue/
- https://developer.adobe.com/media-encoder/uxp/media-encoder-api/render-queue/render-job/
- https://developer.adobe.com/media-encoder/uxp/media-encoder-api/render-options/
- https://github.com/AdobeDocs/uxp-premiere-pro-samples

## MVP Functional Requirements (P0)

### A. Read-only timeline scanning

- Detect the active project and active sequence. Show a clear error if either is missing.
- List the available VIDEO tracks (V1, V2, etc., optionally displaying custom track names). Default to V1 if present, but let the user choose any V track. The UI label V1 must map correctly to the API's zero-based index 0; test this.
- Scan **clip track items** on the selected track, not transitions or empty items. One included guide clip = one export job. Sort strictly by timeline start time.
- Read each clip's START and END in **sequence timeline coordinates**, NOT the clip's source media in/out. Thus trims, source offsets, slowed footage, reversed footage, nested sequences and multicam clips still map to their timeline occupied interval.
- Build immutable planning records: sequence identifier, guide track identifier, guide clip identifier (as reliable as available), guide clip display name, sequence range start/end, expected frame count, output basename, inclusion state, and validation warnings. Use an explicit interval convention internally: `[start, end)`; adapt only at the Adobe API boundary after testing actual out-point semantics.
- Clip intervals must respect exact sequence frame boundaries. Prefer Adobe `TickTime`, exact ticks, rational frame counts, or equivalent lossless representations. Never use raw floating-point seconds as the authoritative representation. Test 23.976/29.97/59.94 and 25 fps to avoid off-by-one-frame errors. Do not assume one universal integer tick-per-frame constant.
- Ignore gaps between guide clips; do NOT create jobs for empty spans.
- Transitions on/near guide cuts may cause the exported range to include a transition if it occurs within the clip's interval; flag transition-boundary ambiguity in the UI when the API permits detection. Never silently shift boundaries as a 'fix'.
- Empty or invalid-duration clips: skip and report. Handle muted/disabled guide clips explicitly: default skip disabled guide clips, with an option or clear explanation if the host's enabled state is ambiguous. Remember that a guide track's content may still be part of the final render if Premiere's visibility allows it.
- Do NOT auto-merge adjacent clips, including clips of the same source file. Each timeline clip should be a separate job.

### B. Queue preview and control

- 'Scan Track' generates a clear list of pending jobs BEFORE any export is queued.
- Show columns: include checkbox, ordinal number, source/clip name, timeline IN, timeline OUT, duration (frames or timecode), proposed output filename, status.
- Allow per-job include/exclude and rename of the output filename in preview.
- Offer 'Select all', 'Deselect all', 'Refresh Scan', and 'Add Selected to Queue'. Optionally 'Start Queue' only once reliable.
- Show total selected job count and approximate total duration in addition to job-by-job ranges.
- Detect changes after scanning (sequence or track identity changed, clips changed, invalid output path) before queuing; require rescan rather than sending stale ranges.
- Expose honest statuses: scanned, ready, adding, queued, rendering, done, failed, cancelled, unsupported. Report errors per job; never display 'queued' unless AME/Premiere confirms that the job was added.

### C. Full-sequence export fidelity

- Each output is equivalent to manually selecting the exact [start, end) range of the ACTIVE sequence and exporting it via Premiere/AME using the chosen export preset. It MUST not export the original source file directly.
- Respect all video tracks and audio tracks according to Premiere's enabled/muted state; preserve sequence-level effects, scaling, color management, effects applied directly to clips, Lumetri Color, adjustment layers, graphic overlays, titles and transitions rendered by Premiere.
- Do not temporarily solo or mute tracks, turn off adjustment layers, disable effects, relink media or simplify the timeline for export.
- Export each range as its OWN standalone file and AME job. One guide clip -> one file, never one multi-clip/concatenated file.
- Export should not add black frames for guide-track gaps or accidentally include neighboring shots.
- Audio should be enabled by default and follow the full sequence's normal mixer behavior for the chosen range. Expose an 'Export audio' option only if supported by chosen Adobe preset/API; don't promise features the API cannot set.

### D. Export configuration

- Required: output directory picker, valid export preset path/selection, predictable output extension and naming preview.
- Prefer using existing Adobe Media Encoder `.epr` presets; do not recreate encoding with ffmpeg (ffmpeg cannot guarantee exact Premiere compositor/Lumetri output).
- First version may support one common preset applied to every job; preserve user-selected Premiere sequence settings as supported by the preset.
- Default filenames from guide clip's display name with a 3-digit ascending prefix, e.g. `001_A.mp4`, `002_B.mp4`, `003_C.mp4`. Append extension from the confirmed preset; do not hardcode `.mp4` when format differs.
- Sanitize illegal filesystem characters, handle long names, Unicode, name collisions, same-name clips and case-insensitive macOS/Windows filesystems.
- Detect duplicates both within the pending queue and against existing output files. Default to non-destructive safe suffixes or explicit conflict resolution; never silently overwrite an existing file.
- Save output directory, preset and naming preferences between uses if UXP storage permits; do not persist project-identifying data unnecessarily.
- Handle unsaved Premiere projects, offline footage, missing AME, nonexistent preset and inaccessible output directory with explicit validation errors.

### E. Sequence and project safety

- The plugin must not make destructive timeline edits. It must not change trims, layer order, media, effects, sequence settings or exports already present in AME.
- Prefer an approach with *per-job custom ranges* and no mutation of the original sequence's in/out points.
- If a supported fallback requires adjusting sequence in/out while enqueuing: snapshot original IN/OUT precisely; serialize modifications and queue operations; ensure AME captures each range independently; restore in/out in `finally` and after a failed step; visibly alert on restoration failure; don't proceed if testing shows race conditions. The user should not be able to accidentally save a modified sequence state due to an unhandled error.
- If temporary sequence duplicates are required as a workaround, they must preserve ALL tracks/effects, be clearly named, and only be cleaned up after job independence is verified. Do not delete them while AME still depends on them. Never silently pollute the project bin.
- Ask the user to save before queueing when necessary. Define whether queued AME jobs read a saved project snapshot or current live Premiere state; warn if later timeline changes may affect queued renders.
- Never expose an unsafe fallback as production-ready just because its API calls return success.

## UI and UX

Create a clean, narrow, dockable Premiere panel. Use Adobe Spectrum UXP components where suitable, with accessible labels and keyboard navigation. Keep it practical and compact.

Suggested layout:

`Track Range Exporter`

- Active sequence: `Sequence Name` (read-only display)
- Guide track: `[V1 / Main Footage v]`
- `[Scan Track]` / `[Refresh]`
- Table of jobs with checkboxes, names, in/out, durations and editable output names
- Destination: `[Choose Folder...]`
- Preset: `[Choose AME Preset...]`
- Export mode: `[Add to AME Queue]` (primary), `[Start Queue]` (secondary only if supported)
- Status/feedback: `12 selected / 12 queued`, errors and warnings

The panel should NOT require placing manual sequence markers, selecting every clip, or changing the guide track's visibility. No editor has to visit Export mode clip by clip.

## Architecture Guidance

Use a modular TypeScript design rather than one giant plugin file. Suggested boundaries (adapt to official UXP build conventions):

- `premiere/sequenceReader.ts`: active sequence, tracks and clip interval extraction.
- `domain/rangePlan.ts`: pure transformation from track clip metadata to export plans.
- `domain/frameMath.ts`: exact time/range validation and testable frame calculations.
- `domain/naming.ts`: safe deterministic output naming/collision resolution.
- `export/queueAdapter.ts`: interface for queueing one sequence+range+file.
- `export/premiereEncoderAdapter.ts`: only if a reliable Premiere export path is verified.
- `export/ameAdapter.ts`: if an AME-side component is necessary and supported.
- `ui/`: Premiere UXP panel components and user-facing state.
- `tests/`: pure-domain unit tests, mocks and integration checklists.

A job plan should be pure data, independent of Premiere's transient object handles. Interface sketch (not an API contract):

```ts
type TimelineTick = string; // lossless Adobe ticks; provide verified conversion helpers
interface ExportJobPlan {
  id: string;
  sequenceGuid: string;
  guideTrackIndex: number;
  guideClipName: string;
  startTick: TimelineTick;    // inclusive
  endTick: TimelineTick;      // exclusive in our model
  expectedFrames: number;
  outputName: string;
  include: boolean;
}
```

No fake interfaces should ship in place of a real integration. Keep Adobe host wrappers isolated for mocking. Avoid native libraries unless UXP cannot support the required workflow. If an AME companion extension is necessary, design and document the communication channel, data format, project snapshot/save requirements, authentication/local permission assumptions and installation steps; do not assume UXP cross-host module imports work.

## Detailed Edge Cases to Implement/Test

1. Guide track with 3 adjacent clips: 3 files, no overlap/gaps/extra frame.
2. Guide track with gaps: gap not exported.
3. V2 overlay extending across two V1 clips: overlay visible in both correct ranges.
4. V3 adjustment layer across whole sequence: grading visible in every exported file.
5. Clip-level Lumetri on V1 and an adjustment layer on V3: BOTH contribute.
6. Additional V2 footage hides V1 during some frames: export what Premiere actually displays, not a soloed V1.
7. Audio not split where V1 is cut: export full-sequence audio in each range.
8. Clip durations differing, e.g. 2 s, 7 s and 1 s: AME job ranges must differ accordingly.
9. Non-zero sequence start time and nonzero user-set in/out: correct timeline-relative export ranges; initial in/out preserved if any temporary mutation occurs.
10. Multiple guide clips from same source or duplicate visible names: unique output files.
11. Reversed, speed-ramped and nested guide clips: use sequence boundaries, not source-media boundaries.
12. Muted guide track, disabled clip, empty track, mixed transition track items: sensible defaults plus clear warnings.
13. 23.976, 25, 29.97 drop-frame, 30, 50 and 59.94 fps: no off-by-one frame in beginning, end or duration.
14. Export jobs queued while AME is running, project/sequence changes during enqueue, and user cancellation: no corrupt state; clear feedback.
15. AME unavailable, preset missing, user cancels directory choice, export fails, file already exists: clear error and no damage.
16. A job followed immediately by another without a gap: first output's LAST frame is the last frame belonging to clip 1, next output's FIRST frame belongs to clip 2.
17. Single clip and dozens/hundreds of clips: UI remains responsive, queueing is incremental, errors identify the affected job.
18. A cut with a dissolve/transition: document the exact cut/transition convention and verify exported frames.

## Testing and Verification Rules

- Write unit tests for ordering, disabled items, gap skipping, naming, collisions, selected jobs, invalid ranges, precise frame math and stale snapshot checks.
- Supply a small reproducible Premiere test fixture or a documented manual fixture recipe: three different source shots with brightly colored overlays and one obvious Lumetri adjustment above them; audio spanning cuts. Include expected start/end frame counts.
- Integration test: build/load panel in Premiere, scan track, verify detected ranges, enqueue to AME, inspect each queued job's source sequence/range/preset/filename, render actual files, and inspect first/last frames plus mid-frame overlays/grade and audio. A successful API return alone is insufficient.
- Do not assert real integration success without executing and checking it in an actual Adobe host. Distinguish unit-tested, host-tested and unverified behaviors.
- Document supported Premiere/AME build versions and platform, UXP manifest permissions, installation, packaging, expected path to preset, and troubleshooting instructions.
- Create `README.md` with a practical 2-minute installation and usage tutorial, `TECHNICAL_DECISIONS.md` with why the chosen queue architecture works, and `KNOWN_LIMITATIONS.md` with actual (not hypothetical) remaining constraints.

## Development Milestones / Definition of Done

**Milestone 1 - Range scanner:** A working UXP panel can enumerate video tracks and produce correct timecode/frame-accurate ranges from selected track clips without changing timeline content. Pure unit tests pass.

**Milestone 2 - Verified two-job export:** Using one full Premiere sequence with an adjustment layer above the guide track, automatically enqueue and actually render TWO clips with different durations/ranges as independent AME jobs. Both carry the adjustment layer. This is the highest-risk milestone and MUST be proven before adding lots of features.

**Milestone 3 - Practical batch workflow:** Naming, destination, .epr preset, multi-job preview, exclusions, queue feedback, error handling, and safe conflict behavior.

**Milestone 4 - Polish:** Persistence, edge cases, cancellation/retry, responsive UI, installation instructions and packaging.

A successful MVP means: with an already-graded sequence, the user can choose V1, review its N individual clip boundaries, choose an export destination and preset, click ONE button, and obtain N correctly ranged full-composition exports queued to Adobe Media Encoder, WITHOUT manually changing in/out and WITHOUT modifying the edited clips or their grades.

## Instructions to the Coding Agent

1. Inspect the repository first and reuse any existing build setup. If empty, scaffold a minimal UXP TypeScript project and record exact environment requirements.
2. Start with a brief feasibility assessment focused on independent export time ranges in AME, including cited Adobe API references. Do not spend days writing UI before proving the hard part.
3. Do not invent Adobe API method names or return types. Read official docs/sample code and verify SDK TypeScript declarations. Treat beta API availability as conditional, not guaranteed.
4. Implement the milestones in order, test after each, and keep a short running `PROGRESS.md` with done, blocked, next steps and verification status.
5. Continue autonomously through reasonable implementation choices. Ask the user only for a genuinely blocking requirement (for example access to a running Premiere/AME instance, exact installed version if crucial, or an export preset file needed for an end-to-end test). Use sensible defaults otherwise.
6. If a proposed method proves unreliable (such as repeated queueing while mutating a single sequence's in/out), change approach rather than shipping a deceptively working plugin.
7. Do not confuse *rendering a clip* with *rendering a full sequence section*. The latter is the product.
8. Final report must say exactly what was built, how to install it, how to test it on a real sequence, which APIs were verified, and which aspects remain untested or unsupported.

## Out of Scope for the First Working Version

- Creating color grades or LUTs.
- Film emulation effects or Dehancer replacement.
- Background cloud rendering, distributed rendering, or custom transcoding engines.
- Automatically splitting/rearranging timeline clips.
- Multiple output formats per guide clip and elaborate batch naming templates.
- Exporting a soloed track or ungraded media.

**Guiding principle: Be a dependable batch exporter for the user's existing finished Premiere timeline. The chosen guide track supplies only the IN/OUT boundaries; Premiere supplies EVERYTHING visible/audible inside those boundaries.**
