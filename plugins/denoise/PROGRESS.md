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
