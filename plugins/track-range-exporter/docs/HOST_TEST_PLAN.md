# Host test plan and fixture recipe

Nothing below has been run yet: the plugin was built on Linux without Premiere Pro or Media
Encoder. Run these on a Mac (or Windows PC) with Premiere Pro 25.6+ and Media Encoder.
Record results in `PROGRESS.md`.

## Fixture (5 minutes to build)

Sequence **TRE Fixture**, 1920×1080, **25 fps**, start timecode 00:00:00:00.

| Track | Content | Timeline position (frames) |
| --- | --- | --- |
| V1 | Color Matte **red** "A" | 0 – 50 (2 s) |
| V1 | gap | 50 – 75 |
| V1 | Color Matte **green** "B" | 75 – 250 (7 s) |
| V1 | Color Matte **blue** "C" | 250 – 275 (1 s), directly after B |
| V2 | Text "OVERLAY" | 25 – 100 (spans the A→gap→B boundary) |
| V3 | Adjustment Layer with Lumetri: Saturation 0, Exposure +1 (obvious) | 0 – 275 |
| A1 | Any continuous audio (e.g. tone) | 0 – 275, **not** cut at clip boundaries |

Optional extra: put Lumetri directly on clip B (tint), so both clip-level and adjustment-layer
grades are visible. Set a sequence In/Out at 10–20 before testing (to check it is restored).

Expected jobs after **Scan Track** on V1 (exclusive OUT):

| Job | IN | OUT | Frames |
| --- | --- | --- | --- |
| 001_A | 00:00:00:00 | 00:00:02:00 | 50 |
| 002_B | 00:00:03:00 | 00:00:10:00 | 175 |
| 003_C | 00:00:10:00 | 00:00:11:00 | 25 |

## Spike test (Milestone 2) — do this first

1. `npm install && npm run build`, load `dist/premiere-panel/manifest.json` in UXP Developer Tool.
2. Open the fixture, Scan V1, choose an output folder and an H.264 or ProRes `.epr` preset.
3. Route A: **Add Selected to AME Queue**. In AME, open each job's Export Settings and check the
   source range. Then render.
4. Check outputs:
   - frame counts: `ffprobe -count_frames -select_streams v -show_entries stream=nb_read_frames 001_A.mp4` → 50 / 175 / 25
     (one more on every file → switch *Advanced → Out point* to "inclusive" and repeat)
   - all three are **grey and bright** (adjustment layer applied), first frame of 003_C is blue and
     the last of 002_B is green (no bleed at the adjacent cut)
   - OVERLAY visible at the end of 001_A and at the start of 002_B
   - audio continuous in every file
   - sequence In/Out is back at 10–20
5. Route B: switch the queue method, write the job file, load it in the AME panel
   (`dist/ame-companion`, AME 27.0+), add jobs, render; check the same points.

**If Route A produces identical ranges for all jobs** (AME did not snapshot per job): Route A must
be disabled before release and Route B becomes the only route.

## Full checklist (spec edge cases)

- [ ] V1 label → API index 0 (scan V1 shows the V1 clips)
- [ ] 23.976, 25, 29.97 DF, 30, 50, 59.94 sequences: frame counts exact
- [ ] Non-zero start timecode (e.g. 01:00:00:00): ranges and timecode labels correct
- [ ] Disabled guide clip excluded by default; included with the Advanced option
- [ ] Hidden (track output off) guide track warns, still exports full composition
- [ ] Reversed / speed-changed / nested / multicam guide clip uses timeline boundaries
- [ ] Duplicate clip names → unique files; existing file in folder → `(2)` suffix, not overwritten
- [ ] Dissolve on a guide cut is flagged; exported frames match the documented convention
- [ ] Edit the timeline after scanning → queueing refused until rescan
- [ ] Cancel during a 50-job run → stops, In/Out restored
- [ ] AME not installed / preset deleted / output folder removed → clear error, nothing queued
- [ ] 200+ clips: panel stays responsive, jobs are added incrementally
- [ ] Undo history: the in/out transactions are labelled "Track Range Exporter: …"
