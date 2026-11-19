# Conductor — specification for coding agents

> Plugin 7 on the roadmap (`../../ROADMAP.md`). Owner's brief (2026-11-19): turn signals, mainly
> **audio**, into **keyframes** on any effect parameter in Premiere Pro, without After Effects.
> Other tools (our effects, Premiere's own effects, third-party effects) are the outputs. Easy to
> use, but "the best thought-through plugin of its kind". **Premiere Pro only** (Resolve is not a
> priority). Not a recreation of any existing product (no names, UI or code of e.g. After
> Effects' "Convert Audio to Keyframes" or third-party audio-to-keyframe tools).

## 1. Product shape

- A **UXP panel** for Premiere Pro (TypeScript, like `plugins/track-range-exporter`), not a
  video effect: only a panel can write keyframes onto parameters of other effects.
- Core unit: a **Conductor** = one or more signal inputs → one shaped control curve → **exactly
  one output parameter** (one effect parameter on the selected clip, or the clip's speed). More
  outputs = more Conductors (a list in the panel, each can be duplicated, linked to the same
  analysis, muted, re-applied).
- **Non-destructive and repeatable:** a Conductor's settings are stored with the project (storage
  mechanism to verify in C0: sequence/project metadata, markers, or a sidecar file next to the
  project). "Update" rewrites exactly the keyframes this Conductor wrote, e.g. after the music
  edit changed. Every write is one undo step.
- Deterministic: same audio + settings → same keyframes, always.

## 2. Inputs (signals)

- **Audio sources:** an audio clip/track item in the active sequence (its media file is read and
  decoded by the panel, mapped through the item's in/out and timeline position), or any audio file
  with a time offset. Several sources per Conductor are allowed.
- Per source:
  - **Frequency band**, chosen visually on a spectrum/spectrogram (e.g. 40-120 Hz for a kick);
    presets: Sub, Kick, Bass, Snare body, Snare/clap crack, Vocals, Hi-hats, Air, Full.
  - Channel: L+R, L, R, Mid, Side.
  - **Detector**: level (RMS or peak, dB) with attack/release smoothing; or **onset/transient**
    (spectral flux in the band) for hits like kicks and snares.
- Combining several sources: max, sum, multiply, "A minus B" (ducking/sidechain).
- Further signal types (useful, later): **sequence markers** as triggers (manual control),
  **tempo/beat grid** (detected or typed BPM, subdivisions, offset) for quantised events, seeded
  random per event, LFO.

## 3. Shaping (input → value)

Two modes per Conductor:

- **Continuous** (the value follows the signal): input window (min dB → max dB, set by dragging
  on the level graph), output range (min → max parameter value), **transfer curve** (linear,
  exponential, logarithmic, S-curve, or a free curve editor), smoothing, invert, gate below a
  threshold.
- **Triggered** (each detected hit creates a shaped event):
  - threshold with hysteresis and minimum gap between hits (retrigger hold);
  - event **envelope**: build-up time before the peak, hold, release after it; **peak offset**
    (default: the event's peak sits exactly on the detected peak of the hit; may be shifted
    earlier/later); **shape curves** for rise and fall (linear, ease-in/out, exponential, bezier
    handles, or a drawn curve);
  - amplitude: fixed, or scaled by hit strength (velocity) through its own curve;
  - overlapping events: maximum, add, or restart;
  - quantise to the beat grid (optional).
- Output combination with existing keyframes/value: replace, add to base value, multiply base.
- **Live preview** in the panel before writing: waveform/spectrogram, band, threshold line,
  detected hits, resulting curve in parameter units.

## 4. Output (keyframes)

- Target: an effect parameter on the selected clip (list effects and their keyframable
  parameters; scalar first, 2D points later as X/Y or as a direction).
- Keyframe density: one per frame, or **optimised** (curve simplification within a tolerance,
  bezier where the host supports it, hold keys for steps) to keep the timeline usable.
- Range: whole clip, sequence in/out, or the selection.
- **Speed (time remapping) output**, e.g. "500 % at every kick peak":
  - speed integrates to source time, so a speed bump shifts everything after it; the panel must
    offer **sync-preserving ramps** (each bump balanced, e.g. a slower section so source and
    timeline time meet again at the next hit / beat) and show the drift otherwise;
  - frame duplicates/blending at extreme speeds are the host's (or later our Retime plugin's)
    business;
  - feasibility through the Premiere UXP API is **unverified** (see C0): the API reads speed
    (`getSpeed`) but has no documented setter; time remapping might be reachable as a component
    parameter with keyframes, or not at all.

## 5. Research items (phase C0, before product code)

1. Keyframes on arbitrary component parameters (`ComponentParam.createKeyframe`,
   `createAddKeyframeAction`, `createSetTimeVaryingAction`, interpolation modes,
   `createRemoveKeyframeRangeAction`) in Premiere 27: correctness, undo grouping, speed with
   thousands of keys, behaviour on point/colour params. API names from `@adobe/premierepro` 27.0
   typings; verify in a host.
2. Time remapping: is it a component/param reachable from UXP? Can its keyframes be written?
3. Audio: `ClipProjectItem.getMediaFilePath()` + UXP file access; decoding WAV/AIFF (own code),
   compressed audio and audio inside video files (decoder choice and licence; WebAssembly in UXP?).
4. Mapping clip time ↔ source time (in point, speed, reversed clips) and sequence frame rate.
5. Where to store Conductor settings with the project.
6. Survey of existing tools (Premiere extensions, AE features) to find gaps, not to copy.

## 6. Phases (each ends with tests + PROGRESS.md)

- **C0** feasibility spike: tiny UXP test panel + host test by the owner (items 1-5).
- **C1** analysis core in TypeScript (decode, band filters, envelopes, onset detection, beat
  grid, curve shaping, keyframe optimisation), unit-tested on synthetic audio with known hits.
- **C2** panel UI: sources, band picker, previews, one Conductor → one parameter, write/update.
- **C3** triggered mode, speed output (if C0 allows), presets.
- **C4** docs, installer (.ccx like the Track Range Exporter), release; owner host tests.

## 7. Honesty rules

Never claim host behaviour (keyframe limits, time remapping, audio formats) without a host test.
Mark unit-tested, host-tested and unverified behaviour separately.
