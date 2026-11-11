# PROGRESS — Track Range Exporter

_Last update: 2026-11-11_

## Verification legend
- **unit**: covered by `npm test` (vitest, no Adobe host)
- **mock**: browser smoke test with a mocked Premiere API (`tests/browser-smoke.cjs`)
- **host**: verified inside Premiere Pro / Media Encoder — basic workflow confirmed by the user on
  macOS (see below); detailed frame checks still open

## Done
- [x] Phase 0 research against official sources (type package, Adobe docs sources, samples) → `TECHNICAL_DECISIONS.md`
- [x] Exact tick/frame math, timecode incl. 29.97/59.94 drop-frame (unit)
- [x] Range planner: sorting, gaps, adjacent clips, disabled/zero-length clips, transitions, warnings, immutable plans (unit)
- [x] Stale-scan fingerprint (unit)
- [x] Naming: sanitising, Unicode, long names, reserved names, case-insensitive collisions, never overwrite (unit)
- [x] Route A adapter: serial in/out + queue, read-back verification, restore on success/failure/cancel/timeline change (unit with fake host)
- [x] Route B: job-file format + validation, AME companion queue logic, preflight (unit with fake host)
- [x] Premiere panel UI + AME companion panel, esbuild bundles, typecheck against `@adobe/premierepro` 27.0.0 types
- [x] Panel end-to-end against mocked API: scan → 3 jobs → queue → In/Out restored (mock)
- [x] Docs: README, TECHNICAL_DECISIONS, KNOWN_LIMITATIONS, HOST_TEST_PLAN

## First host feedback (2026-11-11, macOS, Premiere Pro, installed via .ccx)
- ✅ host: `.ccx` installs via Creative Cloud; panel opens; video tracks are listed (V1 / Video 1).
- ❌ host: the sequence was not shown on first open (needed ↻), and **Scan Track found nothing**;
  the error message was not visible (it was at the bottom of the panel).
- Fixes in 0.1.2 (not yet host-tested): sequence picker (all sequences, follows the active one),
  status line under Scan, robust frame-rate reading (timebase → ticksPerFrame → fps), labelled
  scan steps, **Copy Log** button with host info and step details.

## Second host feedback (2026-11-11, Premiere 27.0.0 build 88, UXP 9.4, macOS arm64)
- ✅ host: sequence picker lists the sequence; Copy Log works (clipboard).
- ✅ host: scan reads 3 video tracks, 8 clips on V1, clip ticks and timebase (10594584000 = 23.976).
- ❌ host: crash `TextEncoder is not defined` while building names; error then hidden because
  `Element.toggleAttribute` is also missing, leaving "Scanning…" forever. Layout broken because
  UXP does not style `<header>/<section>/<footer>/<details>`.
- Fixes in 0.1.3 (not yet host-tested): own UTF-8 length, guarded `normalize`, no
  `toggleAttribute`, error shown before cleanup, div-based layout, refresh on panel show, track
  list reloads when the shown sequence changes. Regression guards: `tests/uxpCompat.test.ts` and
  the browser smoke test now removes those APIs.

## Third host feedback (2026-11-11, macOS)
- ✅ host: v0.1.3 scan works — 8 clips with correct IN/OUT/frame counts on a 23.976 sequence.
- Requests: whole panel scrollable; built-in export presets with fps/bitrate sliders and audio options.
- 0.1.4 (not yet host-tested): whole panel scrolls; Export preset section with 12 destinations,
  fps + bitrate sliders, audio on/off, AAC bitrate, sample rate; templates found in Premiere's /
  AME's installed system presets (by format code + name) and patched (`domain/eprPreset.ts`,
  unit-tested); user's saved AME presets listed; generated preset validated with
  `getExportFileExtension` before queueing. Manifest now needs `localFileSystem: fullAccess`
  (to read Adobe's preset folders).
- Unverified: exact preset folder locations on macOS/Windows, whether AME honours every patched
  parameter (check the job settings in AME).

## Host confirmation (2026-11-11, macOS, Premiere Pro 27.0.0)
- ✅ host (user report): installed v0.1.6 via .ccx, scanned the guide track, queued the clips to
  Media Encoder with the built-in presets — "it works".
- Still to confirm in detail: exported frame counts per file (out-point convention), first/last
  frames at adjacent cuts, Windows, the Media Encoder companion route.

## Blocked
- **Milestone 2 (verified two-job export)**: no Premiere/AME in the build environment (Linux container).
  Needs a person with Premiere to run `docs/HOST_TEST_PLAN.md` → "Spike test".

## Next
1. Run the spike test on macOS; record Premiere/AME versions, out-point convention, per-job snapshot result.
2. If Route A fails the snapshot test → hide Route A, make Route B the default.
3. Package as `.ccx` via UXP Developer Tool after host verification.

## Commands
```bash
npm install
npm test            # 64 unit tests
npm run typecheck
npm run build       # -> dist/premiere-panel, dist/ame-companion
NODE_PATH=$(npm root -g) npm run smoke   # needs Playwright + Chromium
```
