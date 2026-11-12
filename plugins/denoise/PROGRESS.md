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
- No code yet. Nothing tested in a host.
