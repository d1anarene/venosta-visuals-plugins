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

## D1 results (2026-11-12): noise model, synthesis, metrics, blind profile estimate

Tools: `dn_eval_tool profile|baseline [w h]` (tools/dn_eval.cpp); tests: tests/core.

**Synthesis** (`src/eval/dn_synth.*`): linear light → inverse camera matrix → Bayer RGGB →
Poisson–Gaussian noise → bilinear demosaic → camera matrix → optional unsharp mask → encode.
Ground truth = the same chain without noise. Bayer makes the noise spatially correlated, the
matrix makes it channel-correlated (both checked by tests).

**Model change (measured):** a luma-binned profile mis-describes coloured areas. In a red/blue
ramp at equal luma, the luma-channel noise differs 2.5× between grey and colour, because each
channel's noise follows that channel's own value. The profile is now per **RGB channel**, binned
by that channel's value (16 bins), with per-band channel correlation. The opponent-channel noise
follows exactly (W S Wᵀ): the test predicts the measured opponent noise of flat coloured patches
within 5 %.

**Blind estimator** (spatial, one frame): per-bin 10 % quantile of 8×8 block second moments as
the start → iterated rejection (99 % point of pure noise) with truncation-corrected mean,
normalised within each bin → trust test (spread of block variances) → upper envelope from
well-populated bins (|d ln σ / d ln v| ≤ 1.5). Removing each block's mean was dropped: it removed
a spectrum-dependent part of correlated noise (bias 0.85 → 0.92).

Estimate/truth ratios at 960×540 (median per band 0–1 and channel; `dn_eval_tool profile`):

| Content | Result |
| --- | --- |
| Flat patches, white noise | 1.00 |
| Flat patches, Bayer noise | 0.86–0.98 (calibration assumes white noise) |
| Smooth image (skin), low–high noise, with sharpening | 0.84–1.02 |
| Text: background bin | G 0.93, R/B ≈ 0.56–0.9 (edge-adjacent pixels carry dark-pixel noise) |
| Text: edge-only bins | bounded to ≤ ~3× by the envelope (was 20–40×) |
| Saturated colours, darkest bins (ramp, chart) | 0.3–0.7: same channel value, different noise (matrix crosstalk) |
| Uniform fine texture everywhere (weave, foliage) | 1.2–9× over: texture indistinguishable from noise spatially |

**Conclusions:**
1. A single-frame spatial estimate is reliable only where the frame has flat areas. It cannot
   separate uniform fine texture from noise.
2. **R4 (next):** temporal estimation. In static areas the difference between consecutive frames
   (after motion compensation) is noise ×√2 *regardless of texture*. This is the planned primary
   estimator for video, with the spatial estimate as fallback and initial guess.
3. **R5:** calibrate the quantile/truncation statistics for the measured spatial correlation of
   the noise (ratio of band energies), instead of white noise.
4. **R6:** colour crosstalk. Model each channel's variance as a sum of per-channel contributions
   (the physics of a colour matrix), fitted from the data.
5. In any case: the user can scale the profile ("Noise Level") and see the removed noise (View →
   Removed Noise). The estimate is a starting point, never silently trusted.

**Baselines** (PSNR dB / SSIM vs ground truth, medium noise): noisy input, Gaussian σ 0.8, σ 1.5.

| Image | Noisy | Gauss 0.8 | Gauss 1.5 |
| --- | --- | --- | --- |
| weave | 24.24 / 0.909 | 19.33 / 0.485 | 16.98 / 0.131 |
| foliage | 20.09 / 0.917 | 21.63 / 0.938 | 21.67 / 0.847 |
| text | 31.49 / 0.897 | 19.11 / 0.847 | 16.30 / 0.734 |
| skin | 29.76 / 0.739 | 33.75 / 0.923 | 37.60 / 0.980 |
| ramp | 22.62 / 0.561 | 26.21 / 0.750 | 28.89 / 0.865 |
| chart | 22.04 / 0.564 | 23.95 / 0.740 | 24.16 / 0.831 |
| night-street | 20.17 / 0.222 | 23.15 / 0.480 | 24.94 / 0.693 |

Blur helps smooth content and destroys fine detail (weave −7 dB, text −15 dB). The denoiser
must beat the best blur on smooth images **and** keep weave/text at or above the noisy input.
These are the D2/D3 acceptance numbers.
