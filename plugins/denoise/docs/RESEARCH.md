# Denoise — research notes (phase D0)

## Sequence benchmark and probe implementation (2026-11-13)

Implemented the first executable sequence benchmark: static detail, subpixel pan, independently
moving occluder, hard cut and exposure step; known clean/noisy images, analytic backward motion,
visibility/disocclusion and separate skin/fabric/foliage/hair/foreground masks. Optional visible
frame numbers support host tests and are excluded from evaluation masks.

The explicit signal is synthetic linear Rec.709 RGB with independent Gaussian shot/read noise,
encoded with the ACEScct **scalar curve only**, not AP1 ACEScct or a calibrated camera-log model.
The export/scorer preserves floats and dataset/source identity; no denoising algorithm was added.
Usage, metrics and limitations: [SEQUENCE_BENCHMARK.md](SEQUENCE_BENCHMARK.md).

### Measured control run

macOS arm64 / AppleClang 21.0.0, Release CPU; five scenarios × three seeds × 12 frames × five
controls = **900 per-frame records** at 320×180. Exposure −2 stops, shot variance coefficient
0.00005, read sigma 0.0005; default pan 0.65/0.25 pixels/frame. Command:

```bash
build-denoise/dn_sequence_tool benchmark --width 320 --height 180 --frames 12 --seeds 7,19,43 --output /tmp/dn-controls-320.json
```

Runtime source SHA256 reported by the build:
`02d1f03981ae73479e95fcc517c2433113deb28de968998933d851dc31a2fd69`.
These are descriptive means over this small synthetic set, not independent-scene confidence
intervals or a denoiser ranking. All error units below are encoded RGB.

| Case / control | RGB RMSE | Detail transfer | Pooled flat residual std | Disocclusion RMSE |
| --- | --- | --- | --- | --- |
| Static / noisy | 0.004881 | 1.000544 | 0.004647 | unavailable |
| Static / blur 0.8 | 0.009384 | 0.531195 | 0.001664 | unavailable |
| Pan / frozen clean | 0.034091 | 0.108444 | 0.015655 | unavailable |
| Moving occluder / noisy | 0.004781 | 1.000422 | 0.004629 | 0.004970 |
| Moving occluder / unaligned average | 0.009445 | 0.817470 | 0.003385 | 0.033696 |

The controls demonstrate why lower residual variation alone is insufficient: blur removes
structure and unaligned averaging trails behind the moving object. Frozen clean output at a hard
cut has temporal residual RMSE **zero** in the valid within-shot regions while full-frame RMSE is
0.025649; spatial/detail and cut checks must accompany temporal scores. The clean oracle has zero
error and unit detail transfer where supported. It is a scoring sanity check, not a candidate.

### Host probes and verification boundary

Separate OpenFX CPU, Premiere CPU and Premiere Metal probes live in
[`shared/engine/probes`](../../../shared/engine/probes/README.md). They request temporal input,
display current/neighbour/difference, record requested time/hash/quality/scale and test a
user-action persisted mean. Production adapters are unchanged; diagnostics never denoise.

All three compile and load locally against actual Adobe 27.0 and pinned OpenFX headers. Contract
and log-checker tests pass; separate Metal float16/32 pixel tests pass six cases. In restricted
CTest, Metal device access is explicitly skipped, not counted as a GPU pass. Actual Premiere/
Resolve input-stage, timing, UI persistence and export tests remain **unperformed**. Windows CI is
configured, not yet run; Premiere CUDA and OFX GPU probes are not implemented. SDK files remain
outside the repository. See the probe README for the dated SDK revisions and host matrix.

The next evidence is actual same-stage host captures and real-log calibration, followed by spatial
candidate comparisons. E0/E1 have an initial synthetic foundation, not a completed held-out camera
benchmark; D0 remains open until host probes are exercised.

## Review before sequence/probe implementation (2026-11-13)

**Primary target confirmed by the owner:** high-quality camera-log footage with visible sensor
noise. Detailed experiments and gates: [RESEARCH_PLAN.md](RESEARCH_PLAN.md). Primary literature,
SDK sources and reference-use constraints: [SOURCES.md](SOURCES.md).

### What actually exists

- CPU opponent transform, exactly reconstructing **decimated** Laplacian pyramid, per-band RGB
  profiles and spatial/two-frame estimators.
- Camera-like still-image simulation, Gaussian blur baselines, RGB PSNR and luma SSIM.
  Shot noise uses a Gaussian approximation and one bilinear Bayer pipeline; this is not a
  calibrated multi-camera log simulator or a validated low-count Poisson experiment.
- No denoising kernel, motion estimator, temporal image-sequence harness, temporal quality metric,
  host probe, production adapter or downloadable Denoise plugin.
- Current shared Premiere GPU rendering reads only `inFrames[0]`; OFX explicitly disables temporal
  source/effect access. A temporal interface is not already implemented by the shared engine.

### Corrections and unresolved assumptions

1. `(a-b)/sqrt(2)` exposes noise only with cancelled scene content; its variance is
   `(Var(a)+Var(b)-2 Cov(a,b))/2`. Equal-variance independent noise gives the familiar single-frame
   variance. Temporal correlation, fixed patterns, varying exposure, interpolation and imperfect
   registration require additional treatment. R4 below validates mostly static scenes only.
2. Four detail bands × RGB × 16 bins over 0–1 plus per-band correlations do not establish support
   for arbitrary log/HDR or colour crosstalk. The lowpass residual is unmodelled. Orthonormal
   opponent conversion is not full noise whitening; the 5% flat-patch test verifies covariance
   propagation on measured samples, not blind generalisation to mixed-colour scenes.
3. The current report selects populated, trusted bins and prints bands 0–1. It does not report
   full spatial/exposure coverage, all-band errors or confidence intervals. Good medians can
   coexist with severe regional errors; unsupported bins must be explicit in the next harness.
4. A chi-square patch test, an unbiased inverse for arbitrary profile-derived stabilization,
   guaranteed absence of ghosting, and a universally strongest classical method are **not**
   established. Candidate choices require comparison and calibration.
5. Running profile smoothing across rendered frames would depend on render order. Use saved
   locked profiles or deterministic analysis of declared neighbours; never update saved profile
   parameters during ordinary rendering.
6. Premiere CPU layer checkout may return raw source while the current input contains preceding
   effects. Same-stage centre/neighbour pixels, not just successful fetches, are a blocking probe.
   Quality/draft signals are not yet verified export detectors. These SDK-guide findings are
   linked in SOURCES.md; actual host behaviour remains untested.

### Local recheck (2026-11-13)

Source revision: `9979ca4` before this documentation update. macOS arm64, AppleClang 21.0.0,
Release C++17; CPU only. No Premiere, Resolve, Metal or CUDA test was performed.

```bash
cmake -S plugins/denoise -B /private/tmp/venosta-denoise-research-build -DCMAKE_BUILD_TYPE=Release
cmake --build /private/tmp/venosta-denoise-research-build -j 4
ctest --test-dir /private/tmp/venosta-denoise-research-build --output-on-failure
/private/tmp/venosta-denoise-research-build/dn_core_tests
/private/tmp/venosta-denoise-research-build/dn_eval_tool temporal 960 540
/private/tmp/venosta-denoise-research-build/dn_eval_tool baseline 960 540
```

- Build and CTest pass; **12 named tests, 249 checks, 0 failures** in the current source. This
  supersedes the earlier progress note's count of 14 tests; no new implementation tests added.
- Medium-noise baseline reproduced: weave noisy 24.24 dB / 0.9085 SSIM versus Gaussian 0.8
  19.33 / 0.4846; text 31.49 / 0.8968 versus 19.11 / 0.8466; synthetic skin 29.76 / 0.7386
  versus 33.75 / 0.9231. These are measurement/blur results, not denoiser results.
- The temporal report's selected-bin worst estimate/truth ratio is **0.31**; worst per-band
  channel-correlation error is **0.34**. Those failures remain despite reasonable medians.
  Reporting only medians would overstate readiness. No new full-motion/log benchmark was run.

## Earlier D0/D1 record (2026-11-12)

The observations below retain their original experimental context. Conclusions are limited by
the corrections above; future-method descriptions are plans, not implemented capabilities.

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
2. **R4 (done for static shots, see below):** temporal estimation. In static areas the difference between consecutive frames
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

### R4 temporal estimate (two frames, `estimateProfileTemporal`, `dn_eval_tool temporal`)

(a − b)/√2 has exactly one frame's noise wherever the scene is static, and the picture cancels.
Binning uses (a + b)/2, and moving areas are rejected by the same robust statistics. Tested with
the second frame containing a moving block (16 % of the frame shifted by 9 px). Medians per band
0–1 and channel, estimate/truth:

| Image | Spatial | Temporal |
| --- | --- | --- |
| weave | 1.07–6.4 | 0.67–1.02 |
| foliage | 0.97–9.3 | 0.86–1.11 |
| text | 0.97–43 | 0.83–1.15 |
| skin | 0.75–1.02 | 0.78–1.01 |
| ramp / chart (saturated colours) | 0.8–1.0 (dark bins 0.3–0.7) | same (R6: colour crosstalk, not texture) |

The temporal estimate becomes the primary method once neighbouring frames are available (Final
mode), with motion compensation from D4 so that panning shots work too. Single-frame spatial
estimation remains the fallback (Preview, first frame, hosts without temporal access).

## Host evidence: Premiere CPU probe on Windows (owner, 2026-11-13)

Premiere Pro 2027, Windows, NVIDIA GPU, `TemporalProbeCPU.aex`: **fail**.

- **Measure:** the analysis action got no pixels; the saved value stayed 0.00. This fits R3: a UI
  action cannot read the frame on this path.
- **Rendering:** the clip with the probe froze.
- **Installed only:** with the probe merely installed, Premiere's Program Monitor stopped refreshing.

No neighbour-frame evidence was obtained. Consequences until the cause is known:
- The AE-API CPU checkout path is not a basis for temporal Denoise in Premiere on Windows.
- "Auto Profile" must not depend on a UI-action frame read in Premiere (use the render-time
  measurement + "lock" fallback from R3).
- The Premiere GPU path (`GetFrameDependencies`; a CUDA probe does not exist yet) and the Mac
  probes remain untested. Details: `shared/engine/probes/README.md`.

## D2: spatial candidate (a) — profile-aware two-stage Wiener in the a trous domain (2026-11-13)

Code: `src/core/dn_starlet.*` (undecimated B3-spline wavelet, exact reconstruction, shift invariant),
`src/core/dn_spatial.*`, `measureProfileStarlet`. Evaluation: `dn_eval_tool spatial w h luma chroma
window seed [quiet] [recovery]`, **oracle profile** (noise measured from ground truth, so filter quality
is separated from estimation error). Five bands, opponent channels, noise per pixel from the profile
at the local colour (W S Wᵀ).

Tuning set: 960×540 and 480×270, seed 7. Held-out check: 1280×720, seed 11 (not used for tuning).
Defaults chosen: luma 1.0, chroma 1.5, window 1.0, pilot τ 1.0, detail recovery 0.

| Set | Mean gain over the better of noisy / best Gaussian | Worst vs noisy | SSIM vs noisy (mean / worst) |
| --- | --- | --- | --- |
| tuning (960×540, seed 7) | +2.12 dB (20 of 21 cases better) | +0.32 dB | +0.23 / −0.20 |
| held-out (1280×720, seed 11) | +2.26 dB | +0.75 dB | +0.26 / −0.13 |

Per image (held-out, medium noise): text +3.1 dB, chart +4.1, night street +3.6, skin +3.2, ramp +2.7,
weave +1.2, foliage +0.5.

Findings:
- Never below the noisy input, including fine text and weave, where Gaussian blur loses 5–15 dB.
- **Texture limit:** on foliage and weave, SSIM drops below the noisy input (foliage 0.90 → 0.82).
  Texture at the noise amplitude is indistinguishable from noise for a local-energy filter. Window,
  luma strength and detail recovery only move along the trade-off (sweeps in the eval tool).
- A stricter pilot (τ > 1) raises the mean but loses detail, up to −3 dB vs the noisy input at τ 2.
  Rejected (detail first).
- Skin with sharpened noise: 0.8–1 dB behind a strong Gaussian blur (residual low-frequency noise).
  Five instead of four bands helped only marginally.

Next: candidate (b), patch-based (NL-Bayes style), for texture; then temporal fusion. In static or
compensated areas temporal filtering removes noise without touching texture, which is the real fix
for the texture limit above. Estimation in the starlet domain (blind/temporal) is still to do;
these numbers use the oracle profile.

## R1 answered for Premiere GPU on Windows (owner host test, 2026-11-13)

`TemporalProbeCUDA` build 2: Premiere Pro 2027 delivers the requested neighbour frame on the CUDA
GPU path, through `GetFrameDependencies` (one `PrGPUDependency_InputFrame` at sequence time ± k
frames). The protocol follows Adobe's SDK_CrossDissolve_GPU sample: return NoError and **do not
increment ioQueryIndex**; build 1, which incremented it, got only the current frame. The difference
view shows correct alignment (static areas cancel). **The temporal Final mode in Premiere is built
on this GPU path.** Open: upstream-effect stage consistency, cuts and edges, export, Mac/Metal (the
Metal probe has the same protocol fix), Resolve.
