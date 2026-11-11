# PROGRESS — Track Range Exporter

_Last update: 2026-11-11_

## Verification legend
- **unit**: covered by `npm test` (vitest, no Adobe host)
- **mock**: browser smoke test with a mocked Premiere API (`tests/browser-smoke.cjs`)
- **host**: verified inside Premiere Pro / Media Encoder — **nothing yet**

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
