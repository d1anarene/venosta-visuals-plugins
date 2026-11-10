# Premiere host acceptance checklist

Nothing here has been run yet. Record date, macOS, Mac model, Premiere version and results in
`PROGRESS.md`. A ✓ only counts when observed in Premiere.

## A. Stage 1 proof — load, passthrough, CPU vs GPU
- [ ] Effect appears in Effects → Video Effects → Venosta Visuals → Film Effects (and via search)
- [ ] Applied to a clip with all groups off: image identical to effect disabled (difference blend = black)
- [ ] Project Settings → Renderer = Metal: render works, timeline not red when playing 1080p
- [ ] Renderer = Software Only: same look (software path)
- [ ] Export (H.264 + ProRes) matches the Program monitor at the same frame

## B. Scenario A — adjustment layer
- [ ] 3 clips with different Lumetri grades on V1, adjustment layer on V2 with Film Effects
- [ ] Bloom, Halation, Grain each toggle independently; others keep working
- [ ] Every clip keeps its own grade plus the shared finish; transitions between clips render
- [ ] Clip with transparency below (title over black/empty track): no colour appears where alpha = 0

## C. Scenario B — single clip, save/reopen
- [ ] Lumetri → Film Effects on one clip; edit bloom on a lamp, halation on windows, grain amount/size
- [ ] Keyframe Bloom Intensity and Grain Amount; scrub; values interpolate
- [ ] Undo/redo of parameter changes
- [ ] Save, quit, reopen: same values; same frame renders identical grain (export the same frame twice, compare)
- [ ] Preset popup applies values and resets to "(choose to apply)"; values remain editable

## D. Grain behaviour
- [ ] Grain changes every frame with Animate on; static with Animate off
- [ ] Scrub backward/forward, render cache on/off: same frame → same grain
- [ ] 1/2 and 1/4 playback resolution: grain looks like the full-res grain, not bigger blocks
- [ ] 23.976 and 29.97 sequences; speed-changed clip; nested sequence: no frozen or repeating grain
- [ ] Two instances with different seeds: different grain

## E. Formats and colour
- [ ] 8-bit sequence and "Render at Maximum Bit Depth" (32f) sequence: no banding/clipping differences beyond expected
- [ ] HDR / wide-gamut sequence (if used): super-whites bloom without clipping; choose Working Space accordingly
- [ ] Master off / Mix 0 %: bit-identical to bypass in an export comparison

## F. Robustness
- [ ] 10+ minute timeline, several instances, playback and export without crash
- [ ] Remove the metallib from the bundle: Premiere falls back to software (no black frames)
- [ ] Quit/relaunch Premiere with the effect in an open project

## G. Performance (fill in docs/PERFORMANCE.md)
- [ ] 1080p25 and 4K25: playback dropped frames, export time, per-frame time (GPU and software)
