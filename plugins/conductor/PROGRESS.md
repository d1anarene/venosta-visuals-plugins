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

## 2026-11-20 — C0 host test, round 2 (owner, Premiere Pro 27.0, macOS)

Host-tested (owner: "all steps worked"):
- **598 keyframes** (one per frame, whole clip) onto Motion › Scale in one transaction: 40 ms;
  bezier interpolation on all: 19 ms; values read back exactly. Undo, smooth playback: reported
  as working.
- **Time remapping is not reachable from UXP**: no speed / time-remapping parameter appears in
  the clip's component chain, so Conductor cannot write Premiere's own speed ramps. Speed output
  needs another route (see AGENTS.md §4: a keyframable time parameter of our own Retime effect).
- **WebAssembly runs in UXP** (`typeof WebAssembly` = object, instantiate works), but the first
  instantiate took 5.2 s; 0.0.3 measures a second instantiate to tell start-up cost from
  per-module cost.
- Plugin data folder read/write works (per user and plugin, not per project; project-bound
  storage still open).
- Time base: with a trimmed clip (in point 1.92 s) a key written at in + 1 s read back at
  2.92 s; which base matches "1 s after the clip start" in Effect Controls is still to be
  confirmed by the owner (0.0.3 logs clip start / in with every write).
- Panel layout overlapped on a small screen (flex column shrinking its children): fixed in 0.0.3.

## 2026-11-20 — C1 analysis core (unit-tested, not in a panel yet)

`src/core/` (TypeScript, pure and deterministic), 21 tests in `tests/core.test.ts` on synthetic
music with known hit times (pitch-dropping kicks, hi-hats on the off-beats, a modulated bass,
noise):
- **Decoding:** WAV (PCM 8/16/24/32, float 32/64, extensible) and AIFF/AIFC; channel modes
  mix / left / right / mid / side. MP3/AAC still open (WebAssembly decoder or Premiere export).
- **Bands:** zero-phase band-pass (4th-order Butterworth edges, forwards and backwards) with
  presets (sub, kick, bass, snare body/crack, vocals, hi-hats, air). Kick band passes 70 Hz,
  rejects 1 kHz by > 60 dB; no systematic delay of hits (mean shift < 15 ms, a third of a frame).
- **Envelope:** centred RMS at 1 ms steps (no lag), attack/release smoothing as a separate
  artistic step.
- **Hits:** rise above the loudest level of the preceding 60 ms (so kick-tail/bass beating does
  not count), adaptive threshold, level gate, minimum gap, re-arm hysteresis; reports onset,
  peak time and peak level. Tests: every kick found within 10 ms and nothing else in the kick
  band (12 s, 124 BPM), every hat in the hi-hat band, a tempo drifting 3 ms per beat followed
  exactly (no grid), loud vs soft kicks separated by > 6 dB.
- **Tempo / beat grid:** autocorrelation with a prior around 120 BPM, then a fine tempo/phase
  comb search: 96/120/128/140 BPM within 0.3 BPM, phase within 20 ms.
- **Shaping:** curves (linear, ease in/out, power, exponential, cubic bezier; monotonic, fixed
  ends); continuous mode (input dB window → output range, curve, invert, gate, smoothing);
  triggered mode (rise, hold, release, peak offset, onset/peak alignment, rise/fall curves,
  fixed or velocity amplitude, overlap max/add/restart) — event peak exactly on the hit peak.
- **Keyframes:** one per frame at exact tick times (23.976 fps checked), simplification within a
  value tolerance (fewer than half the keys for the test curve, error ≤ tolerance).
- **Speed (for Retime later):** sequential integration (drift reported, ramp peaks on the hits)
  and compensated mode (source and timeline meet at every hit, zero drift; reports a minimum
  speed ≤ 0 when a ramp cannot be balanced, e.g. 500 % on every beat, for a panel warning).

## 2026-11-20 — C2 first panel (built; not yet run in Premiere)

- UXP panel "Conductor" (`src/panel`, `public/`), German/English by Premiere's UI language:
  1. music = selected audio clip (WAV/AIFF decoded; MP3/AAC message), mapped through its in/out
     and timeline position; 2. band (presets or Hz) and channel; 3. reaction: per hit (build-up,
     hold, release, peak offset, rise/fall curves, fixed or loudness strength, overlap) or
     follow the level (dB window, curve, smoothing, invert); 4. target = numeric keyframable
     parameter of the selected video clip, value at rest / at the peak; 5. one key per frame or
     optimised (tolerance). Live preview graph (level, hits, curve, keys). "Write" replaces the
     parameter's keyframes over the clip in one undo step; "Remove" clears them. Settings kept
     per user (localStorage).
- `plan.ts` (pure, 9 tests): hits only inside the audio clip's used range; keys on the sequence
  frame grid, written in the target clip's media time (assumed from C0; to be confirmed in the
  host — `keyTimeBase` switches to sequence time); **event peaks snap to the nearest frame**
  (otherwise per-frame keys miss short kick peaks: 117 instead of 120 in the test); optimised
  keys within tolerance.
- Browser smoke test with stubbed UXP: renders, German strings, no script errors.
- CI: `.ccx` as artifact `conductor-panel-test-build` (not released).

## 2026-11-20 — owner's first panel test → MP3, EQ band picker, clearer messages

- Owner (Premiere 27, macOS): "Write" answered "choose music and target first" although both
  were chosen — the music was an MP3, which the panel could not decode yet; the message was
  misleading. Now each missing piece has its own message, and decode problems show under step 1.
- **MP3:** mpg123 (WebAssembly, `mpg123-decoder` 1.0.3; licences in THIRD_PARTY_NOTICES.md).
  Measured: honours the encoder delay (first kick 0.2508 s in WAV and MP3; a pure-JS decoder,
  js-mp3, was 47 ms late and took 28 s for 3 minutes, so it was rejected); 3 min in ~0.4 s in
  Node; the bundled decoder runs in Chromium (64 ms for the 6 s fixture). Not yet run in UXP.
  The web-worker part is replaced by a stub at bundle time. Audio inside video files (AAC): open.
- **EQ-like band picker** (owner's request): average spectrum (Welch, 4096-point FFT, unit-tested)
  of the used audio on a log 20 Hz–20 kHz axis; band shaded; drag the edges or drag inside the
  band to move it.
- Browser end-to-end test with a stubbed Premiere API (audio MP3 + video clip + Scale): decode,
  8 hits, EQ drag 120 → 245 Hz, 64 keys written in one transaction, peak keys at 2.25, 2.75, …
  media seconds (clip in point 2 s, start 1 s): exactly on the kicks. Still not run in Premiere.

## 2026-11-20 — 0.1.1: panel did not start in Premiere

- Owner: with the MP3 build the panel showed no texts, empty menus and "nan" fields — the script
  had not run. The MP3 decoder embeds its WebAssembly as `String.raw` literals with ~28 000
  control / non-ASCII characters (158 NUL bytes); Chromium accepts that, Premiere's UXP loader
  apparently not (cause inferred, not proven). Fixes: the build turns those literals into
  ASCII-only string literals with the same value and refuses to finish if any non-ASCII byte
  remains; the decoder is a separate `mp3.js` so a failure there cannot stop the panel;
  `boot.js` shows any script error inside the panel. Browser end-to-end test passes again.

## 2026-11-20 — 0.1.2

- Host result 0.1.1: **the panel starts in Premiere** (texts, menus, EQ view), so the ASCII
  rewrite / separate decoder bundle fixed the start-up; MP3 then failed with "TextDecoder is not
  defined" (UXP has no TextDecoder). 0.1.2 installs a small UTF-8 TextDecoder (and
  performance.now) only when missing, before the decoder loads (unit test against the real one;
  browser end-to-end test with TextDecoder deleted passes). Invalid saved band values fall back
  to the kick preset.

## 2026-11-20 — Premiere crash report, 0.1.3

- Owner sent a Premiere 27.0 crash report (Sentry envelope + minidump, macOS arm64, Metal):
  fatal "Unhandled signal" after 9 minutes; the crashing thread's stack goes from the UXP
  JavaScript runtime (`libdynamic-napi`) into `abort` in libsystem_c — the typical pattern of a
  fatal error in the panel runtime, for example running out of memory. Two `uxp
  uncaughtJSException` breadcrumbs at start-up. Not proven to be Conductor.
- Conductor's memory per recompute on a 130 s song was high (several 23 MB copies and a 46 MB
  prefix-sum array, recreated on every change). 0.1.3: the envelope sums each window directly
  (no prefix array), the analysis channel is computed once per song, recompute debounce 300 ms.
  Measured in Node: 130 s stereo 44.1 kHz analysed in 0.47 s, re-analysed in 0.25 s, ~49 MB.
