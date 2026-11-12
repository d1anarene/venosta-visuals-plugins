# Denoise — research notes (phase D0)

## R1 Temporal frame access

| Host / path | Finding (2026-11-12) | Source | Status |
| --- | --- | --- | --- |
| Premiere GPU (PrGPUFilter) | `GetFrameDependencies` may return `PrGPUDependency_InputFrame` with `outSequenceTime` (and `outTrackID` 0 = current track), one per call, incrementing `ioQueryIndex`. `Render` then receives `inFrames[0]` = current time and the extra frames "in the same order as returned from GetFrameDependencies". | `PrSDKGPUFilter.h`, Premiere Pro 27.0 C++ SDK (private SDK repo, not committed) | API confirmed in headers; **host behaviour unverified** (clip edges, sequence vs clip time, speed changes) |
| Premiere CPU (AE API) | Premiere's own SDK sample `SDK_CrossDissolve_CPU.cpp` uses `PF_CHECKOUT_PARAM` on a layer parameter in Premiere, so the call is available. Checking out the effect's **own input** at other times in Premiere is not shown by a sample. | Premiere SDK examples | Needs a host probe |
| OFX / Resolve | Plan: `kOfxImageEffectPropTemporalClipAccess`, `getFramesNeeded`, `fetchImage(t ± k)`. | OFX spec (to be checked against the fetched OpenFX headers) | Needs a host probe |

The online AE SDK guide (ae-plugins.docsforadobe.dev) is not reachable from this build environment;
nothing above relies on it, and nothing is filled in from memory.

**Probe plan:** an engine-level "temporal probe" mode (debug build only) that renders the
difference between frame t and the fetched t−1, so a host test shows immediately whether
neighbouring frames arrive, and whether they are the unprocessed input.

## R2 Preview / Final signal
- Premiere GPU: `PrGPUFilterRenderParams::inQuality` (`PrRenderQuality`: Draft 0 … Max 4) and
  `inDownsampleFactorX/Y` exist (`PrSDKGPUFilter.h`, `PrSDKQuality.h`). Which values Premiere uses
  during playback vs export: **unknown until host-tested** (log them in a probe build).
- Premiere CPU: `PF_InData::quality` (PF_Quality_LO/HI) exists in `AE_Effect.h`; same caveat.
- OFX: render-quality-draft property (OFX 1.4); Resolve support unknown.
- Decision until measured: default **Final**; *Auto* only where a probe proves the host signal.

## R3 Analyse a frame from a UI action (Auto Profile)
- Premiere SDK documents setup buttons only for its own filter API (`fsHasSetupDialog`), not an
  AE-API button parameter. Probe: AE `PF_Param_BUTTON` + `PF_Cmd_USER_CHANGED_PARAM` with layer
  checkout in Premiere.
- Fallback that needs nothing from the host: a checkbox "Measure Noise" that, while on, estimates
  the profile during render and shows it; the measured numbers are then typed/locked into the
  profile parameters by the user ("Lock Profile").

## Literature to work from (maths only; no code copied)
- Dabov, Foi, Katkovnik, Egiazarian — BM3D (2007); V-BM4D (Maggioni et al. 2012).
- Lebrun, Buades, Morel — NL-Bayes (2013); Arias, Morel — video denoising via empirical Bayes on
  space-time patches (2018).
- Buades, Coll, Morel — Non-local means (2005).
- Foi et al. — Poisson–Gaussian noise modelling and fitting for single-image raw data (2008);
  Mäkitalo & Foi — optimal inverse of the generalized Anscombe transform (2013).
- Liu, Tanaka, Okutomi — noise level estimation using weak textured patches (2012/2013).
- Kroeger et al. — DIS: fast optical flow using dense inverse search (2016).
- Wang et al. — SSIM (2004), for the evaluation harness.
