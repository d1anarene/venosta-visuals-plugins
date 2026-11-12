# PROGRESS — Denoise

_Last update: 2026-11-12_

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
- 14 tests pass. Measured estimator accuracy and limits, plus blur baselines: docs/RESEARCH.md
  ("D1 results"). The spatial estimate is reliable on frames with flat areas only.
- R4: two-frame temporal estimate. Texture cancels in static areas: weave/foliage/text go from
  3–43× over to 0.7–1.15 of the truth, with a moving block in frame. Becomes the primary
  estimator with motion compensation (D4).
- Found and fixed in the shared engine: `vv::gaussianBlurDirect` / `gaussianBlurAccumulate`
  overflowed a 3-entry stack array for planes with 4+ channels (no existing plugin used that).
  Results for 1–3 channels are unchanged; regression test added.
