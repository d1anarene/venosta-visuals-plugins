# PROGRESS — Denoise

_Last update: 2026-11-13_

## Current status: research foundation, no denoiser yet

- **Sequence/probe milestone implemented (2026-11-13).** `dn_sequence_tool` generates five
  deterministic log-curve scenarios with clean truth, motion/visibility, disocclusion and detail
  regions. Includes negative controls, JSON per-frame scoring, versioned float datasets and an
  external candidate scorer with integrity/provenance checks. Optional numbered host fixtures.
- Signal is ACEScct scalar curve on synthetic linear Rec.709 RGB with independent Gaussian
  shot/read approximation; not AP1 ACEScct or calibrated camera-log footage. Natural captures,
  sensor/ISP/log calibration, strong reference runs and a held-out benchmark remain pending.
- Generic research probes in `shared/engine/probes`: OpenFX CPU, Premiere CPU and Premiere Metal;
  temporal inputs, current/neighbour/difference, diagnostics and user-action persistence. Locally
  compiled/loaded against legitimate SDKs kept outside git; actual host verification **not done**.
- Denoise's four CTest targets pass, including existing core, sequence, metrics and CLI tests.
  The same four targets also pass with AddressSanitizer and UndefinedBehaviorSanitizer enabled.
  Five probe CTests pass; the restricted run explicitly skips Metal. A separate GPU run passes
  six Metal pixel cases. All other plugins' CPU/core and configured GPU-emulation regression tests
  pass locally, including Track Range Exporter's 79 tests. CI has not been run for this change.
- 320×180 control run: 900 per-frame records across five scenarios, three seeds, 12 frames and five
  controls. Blur erases texture; frozen/trailing controls fail companion metrics. Results and
  source digest are recorded in `docs/RESEARCH.md`; usage in `docs/SEQUENCE_BENCHMARK.md`.
- Next: actual same-stage host captures and permitted real-log material; profile coverage/
  calibration and stronger baselines before choosing a denoising algorithm. No production plugin.

## Research review before sequence/probe implementation (2026-11-13)

- Owner priority confirmed: **high-quality camera-log footage with visible sensor noise**.
  Skin, hair, fabric, foliage and low-light colour in motion come first; compression is secondary.
- D0 and D1 are incomplete. No denoising algorithm, motion estimation, video-sequence benchmark,
  temporal host probe/adapter, GPU denoiser or downloadable build exists.
- Added `docs/RESEARCH_PLAN.md`: input/capture contract, held-out evaluation, profiling/motion
  experiments E0–E7, same-stage host probes, algorithm comparisons, Preview/Final, performance
  and qualification gates. Added `docs/SOURCES.md` with primary references and usage constraints.
- Audited mathematical assumptions: temporal differences require independent/aligned noise;
  current bins/covariance do not establish log/HDR support; no ghosting or arbitrary unbiased
  inverse guarantee. Saved or fixed-window profiles replace render-history-dependent smoothing.
- Rebuilt locally on macOS arm64 / AppleClang 21.0.0: CTest passes, **12 tests / 249 checks**.
  Re-ran 960×540 Gaussian baselines and two-frame estimator report. Selected-bin worst ratio
  0.31 and correlation error 0.34 remain; no claim that profiling is solved. Details in RESEARCH.md.
- No real-camera capture, external baseline run, commercial comparison or host test performed.

## Gates recorded before this milestone

1. E0/E1: log/capture manifests, sequence ground truth, temporal/detail metrics, all-band profile
   coverage and permitted reference runners. Freeze held-out scenes before algorithm tuning.
2. E3 in parallel: minimal temporal/profile-action host probes; prove same-stage input and timing.
3. E2: held-out log/covariance/lowpass profiling experiments, then E4/E5 CPU candidate comparisons.
4. E6/E7 only after selection: GPU implementation, measured preview/final performance and host
   qualification. The detailed plan records deliverables and advancement criteria.

## Earlier development record (2026-11-12)

Historical findings below are limited by the current review in `docs/RESEARCH.md`.

## Phase D0: plan (in progress)

- Specification and research plan written (`AGENTS.md`): noise model (signal-, band- and
  channel-dependent profile, variance stabilisation), Final mode (motion-compensated temporal
  fusion + two-step spatial stage, candidates pyramid-Wiener and NL-Bayes), Preview mode,
  evaluation protocol with ground truth, phases D0–D7.
- Open research items before temporal code: R1 temporal frame access in Premiere (CPU and GPU) and
  Resolve; R2 a host signal for final renders (Preview/Final "Auto"); R3 analysing a frame from a
  UI action (Auto Profile).
- SDK findings (docs/RESEARCH.md): Premiere GPU frame dependencies confirmed in the 27.0 headers
  (`PrGPUDependency_InputFrame`), `inQuality` available; CPU and OFX temporal access need probes.
- Nothing tested in a host.

## Phase D1: measurement tools (in progress)

- CPU core: orthonormal opponent transform, Laplacian pyramid (exact reconstruction), noise
  profile per RGB channel × value bin × band with channel correlation (predicts opponent noise of
  coloured patches within 5 %), blind single-frame estimator.
- Evaluation: camera-like noise synthesis with ground truth (Bayer, colour matrix, sharpening),
  PSNR, SSIM, procedural test images (weave, foliage, text, skin, ramp + engine scenes),
  `dn_eval_tool`.
- Initial note reported 14 tests; current suite has 12 named tests (verified above). Measured
  estimator accuracy and limits, plus blur baselines: docs/RESEARCH.md
  ("D1 results"). The spatial estimate is reliable on frames with flat areas only.
- R4: two-frame temporal estimate. Texture cancels in static areas: weave/foliage/text go from
  3–43× over to 0.7–1.15 of the truth, with a moving block in frame. Becomes the primary
  estimator with motion compensation (D4).
- Found and fixed in the shared engine: `vv::gaussianBlurDirect` / `gaussianBlurAccumulate`
  overflowed a 3-entry stack array for planes with 4+ channels (no existing plugin used that).
  Results for 1–3 channels are unchanged; regression test added.

## Phase D2: spatial candidate (a) (2026-11-13)

- Undecimated a trous transform + profile-aware two-stage empirical Wiener, opponent channels.
- With the oracle profile: +2.1 dB (tuning) / +2.3 dB (held-out) over the better of noisy input
  and best Gaussian blur; never below the noisy input. Texture limit (foliage/weave SSIM) and
  sharpened-noise skin limit measured and documented (docs/RESEARCH.md, D2).
- Tests: exact reconstruction and shift invariance, flat-field noise removal (> 10 dB), identity
  with a zero profile, alpha unchanged, text above the noisy input.
- Not in a plugin yet; nothing tested in a host.

## Phase D4 (first pass): temporal candidate T1 (2026-11-13)

- Motion-compensated, reliability-weighted temporal average + spatial stage with a noise-scale map.
- On Codex's sequences (held-out seed/size) vs spatial (a): RMSE −20–30 %, detail transfer
  0.61–0.72 → 0.78–0.91, temporal flicker and disocclusion error lower; cuts rejected (no ghosts).
- Premiere (Windows, CUDA): the neighbour frames needed for this are delivered on the GPU path,
  at the same processing stage (host test 2026-11-13, see docs/RESEARCH.md).
- Tests: motion estimation (integer and sub-pixel shifts), temporal better than spatial on a static
  shot, cut rejected. Not in a plugin yet.

## Research prototype effect (2026-11-13) — not released

- `src/effect/DnEffect.cpp`: Denoise as a vv::Effect, CPU only. Premiere's CPU path is spatial only:
  it gets no neighbour frames, and the CPU checkout probe disturbed Premiere. OpenFX does temporal
  fusion through the new engine interface (`maxTemporalRadius` / `renderTemporal`, temporal clip
  access). Profile estimated every frame: two-frame temporal estimate when a neighbour exists,
  otherwise the spatial estimate. Controls: Mix, View (result / removed noise / input), Temporal
  Frames, Luma / Chroma Strength, Detail, Noise Level.
- Spatial blind estimate in the a trous domain: coarse bands were overestimated (up to 18x, image
  structure). They are now capped by the measured band1/band0 noise ratio. On skin the blind result
  went from 32.6 to 37.3 dB (oracle 38.4).
- CI builds installers as workflow artifacts (`.github/workflows/denoise.yml`); it is **not in
  release.yml**. Slow (CPU) and untested in any host. Profile estimation per frame can flicker.
- Tests: effect through the vv::Effect interface (spatial, temporal window, missing neighbours,
  bypass, views, alpha); OFX bundle load test (exports only the entry points).

## D6 (first pass): spatial filter on the GPU (2026-11-14) — not run in a host

- The spatial filter (a) as backend-neutral GPU passes (`src/gpu/common`), run by the shared
  CPU emulation backend in a test (32f 0.0001/255, 16f 0.124/255 vs the CPU reference), and by
  new Metal (`src/gpu/metal`) and CUDA (`src/gpu/cuda`) engines. View and Mix are applied in the
  final pass.
- Premiere: GPU renderer (Metal on macOS, CUDA on Windows), spatial only for now. The noise
  profile is estimated on the CPU from a centred crop (at most 1024x576) read back from the GPU
  frame; the CPU paths use the same crop, so CPU and GPU estimate from the same pixels.
- Resolve (OpenFX) stays CPU on purpose: the GPU renderers are spatial only and the OpenFX GPU
  path has no CPU fallback, so a GPU build would lose temporal fusion there.
- CI: Metal-vs-CPU comparison (`denoise-metal` in tests.yml); CUDA is compiled in the Windows
  Premiere build. CUDA has not been compiled locally or run anywhere yet. Nothing host-tested.
- Next: temporal fusion on the GPU, and multi-frame GPU dependencies in the Premiere adapter
  once probe build 3 confirms the protocol.

## First host feedback and changes (2026-11-14)

- Owner test, Premiere Pro 2027, Windows, CUDA (build 1.1.6 of the prototype): the effect works
  and removes noise, but playback is **not smooth**; luma/chroma defaults too strong.
- Cause found: the per-frame noise estimate on the CPU (1024x576 crop) took about 315 ms on a
  4-core container. Now about 180 ms with identical output (bit-exact check: one starlet
  decomposition instead of two, parallel block statistics, clamp-free interior of the B3 filter).
- New **Quality: Preview / Final** (disk ID 9, default Final): Preview estimates on a 384x216 crop
  (about 20 ms here); the profile then varies more from frame to frame (synthetic 1080p noise:
  6.9 % instead of 1.7 % mean change of the band 0-2 sigmas), so possibly some flicker.
- Defaults halved: Luma Strength 0.5, Chroma Strength 0.75 (owner's request).
- New **Preset** menu (disk ID 10): Light, Medium (= defaults), Strong, Chroma Only, Low Light.
- Not yet measured in a host: playback speed after these changes. Real-time needs the estimate on
  the GPU or a stored profile (R3).

## GPU passes merged, timing view (2026-11-14)

- Owner test 2: 4K footage, GTX 1080, Premiere (CUDA): noise reduction looks good, but Preview
  still lags a lot. Owner's condition: no loss of quality for speed.
- Same maths, fewer passes: the RGB lowpass for the noise lookup is now the inverse opponent
  transform of the opponent lowpass (the starlet is linear, the transform orthonormal) instead of a
  second decomposition, and the vertical starlet step computes band, energy and noise variance in
  the same pass. Per band 4 full-frame passes + 2 pyramid blurs (was 8 + 2); 9 full-frame buffers
  (was 11). Emulation vs CPU reference unchanged (32f 0.0001/255, 16f 0.124/255).
- New View item **Timing (diagnostic)**: result plus a banner with path (CUDA / METAL / CPU),
  frame size, noise-estimate and filter times. Banner code moved to the shared core
  (`vv/overlay.h`, used by the probes too). Speed on the owner's GPU not measured yet.

## Noise estimate on the GPU (2026-11-14)

- Owner's timing (Premiere 2027, Windows, GTX 1080, 4K 32f, CUDA path confirmed): Final
  estimate 202 ms + filter 87 ms = 289 ms; Preview estimate 71 ms + filter 87 ms = 159 ms. The
  CPU estimate was the bottleneck (slower than in our container, probably competing with 4K
  decoding).
- CPU estimator, identical output (hash over profiles of 4 frames x 3 domains unchanged): exact
  quantiles with nth_element instead of full sorts, the 15 (band, channel) curves in parallel,
  interpolation positions computed once per block. 1024x576 in the container: 180 -> 96 ms;
  statistics alone 43 -> 16 ms.
- GPU renderers: crop, starlet decomposition and 8x8 block statistics now run on the device; only
  the block statistics (a few MB) come back for the statistical part (estimateFromBlocks).
  Emulation vs CPU estimate: worst relative sigma/correlation difference 1.1e-6 (float block sums
  instead of double), all three frame layouts. Metal test extended accordingly (CI). Speed in the
  host not measured yet.
