# Denoise — specification and research plan for coding agents

> Plugin 5 on the roadmap (`ROADMAP.md`). Owner's brief (2026-11-12): *"an alternative to Neat
> Video that has awesome noise reducing capabilities while preserving detail … don't make it quick,
> use real maths and thorough planning … an easier-to-compute preview during timeline work that can
> be switched on and off, and the real deal when rendering."*
> Built on the shared engine (`shared/engine`) for **Premiere Pro** and **DaVinci Resolve (OFX)**,
> CPU reference + Metal + CUDA. Repository rules: root `AGENTS.md`.
>
> Not a recreation of any product: no proprietary code, names, UI or assets. Published research is
> used for the maths and cited in `docs/RESEARCH.md`. Reference implementations from papers may be
> run **offline for comparison only**; their code is never copied into this repository.

## 1. What "good" means (acceptance, measurable)

**Owner priority (2026-11-13): high-quality camera-log footage with visible sensor noise.**
Preserve skin, hair, fabric, foliage and low-light colour in motion. Compressed footage is a
secondary stress test. The current research gates, benchmark protocol and experiment order are
in `docs/RESEARCH_PLAN.md`; the evidence and source register are in `docs/RESEARCH.md` and
`docs/SOURCES.md`. Algorithm descriptions below are hypotheses to compare, not a frozen design.
"Market leading" is an ambition requiring comparative evidence, not a current product claim.
The first sequence benchmark and independent host probes are implemented; use
`docs/SEQUENCE_BENCHMARK.md` for exact signal/mask/metric contracts and build instructions.
Neither synthetic controls nor compiled probes establish native camera-log or actual host support.

1. **Noise gone, detail kept.** On the evaluation set (§6), at matched noise reduction the result
   keeps more fine texture than Gaussian/bilateral baselines, and is compared with strong classical,
   learned and available licensed commercial references. Report PSNR/SSIM, colour/detail errors,
   motion artefacts and quality–time–memory curves; report gaps and per-scene regressions honestly.
2. **No new artefacts.** No blotches, "plastic" skin, ringing, temporal shimmer, ghosting or smearing
   behind moving objects. Static areas must not "boil"; moving areas must not trail.
3. **Temporal stability.** Frame-to-frame difference of a static denoised scene ≪ that of the input
   (measured), without freezing real motion. Temporal metrics must account for clean motion and be
   paired with detail retention and disocclusion/trailing measurements.
4. **Predictable, deterministic.** Same frame + parameters → same pixels in any render order, CPU
   and GPU within documented tolerance. No randomness at all.
5. **Fast preview, full-quality render.** A *Preview* mode usable while editing (target: interactive
   at 1080p on an Apple-silicon GPU — to be measured, never claimed without measurement) and a
   *Final* mode with the full algorithm. The switch is visible and explicit (§4.4).
6. Alpha unchanged, bit-exact bypass, mix, no NaN/Inf, HDR/float values handled, resolution
   independence. Optical/motion search sizes may scale with resolution; measured sensor-noise
   correlation lengths must follow the actual input sampling, including proxies and downsampling.

## 2. Noise model (the foundation — everything else depends on it)

Footage that reaches an effect is *processed* camera noise: debayered, colour-matrixed, gamma
encoded, sharpened and compressed. So the model must be richer than i.i.d. Gaussian:

- **Signal dependence:** noise variance depends on brightness (shot noise ∝ signal before the
  transfer curve, read noise constant). Model per channel: σ²(I) as a piecewise-linear function of
  intensity over bins as an initial approximation. Coverage through log curves, grading and
  clipping must be measured; a small lookup table cannot be assumed universally sufficient.
- **Spatial correlation (colour of the noise):** debayering, scaling and sharpening make noise
  low-pass / band-pass. Model: σ per pyramid band (fine → coarse, 4–5 bands).
- **Channel correlation:** chroma noise is often larger and coarser than luma noise. Work in an
  orthonormal opponent space (luma + two chroma axes, e.g. the 3×3 orthonormal DCT/opponent
  transform). An orthonormal transform does not guarantee independent noise: propagate covariance,
  measure residual correlation, and compare full covariance/whitening with marginal variances.
- **Temporal:** random sensor noise can be independent frame to frame, but fixed-pattern components,
  upstream temporal processing and compression need separate tests and covariance treatment.

**Profile** = σ(band, channel, intensity bin), ~4–5 × 3 × 8 numbers.

This is the original candidate. D1 currently implements four **decimated** detail bands × RGB ×
16 bins over 0–1 plus per-band channel correlations; it does not model the lowpass residual.
`docs/RESEARCH.md` records its colour-crosstalk limits. Before freezing a profile format, evaluate
all bands, negative/extended-range and camera-log values, full covariance, spatial noise spectra,
unsupported-bin coverage and held-out accuracy. Never infer a camera's log encoding from pixels.
Profile the exact image domain supplied by the host. Prefer placement before creative grading,
sharpening and added grain; camera log requires explicit transfer/gamut/range handling.

**Estimation (automatic):** robust statistics on weakly-textured areas. Candidates to evaluate:
1. Block-based: split each band into blocks, keep the blocks with the lowest local variance per
   intensity bin (lower percentile / MAD), correct for the bias of taking minima.
2. PCA of patches (Liu, Tanaka, Okutomi 2012/2013: "noise level estimation using weak textured
   patches"): smallest eigenvalue of the patch covariance ≈ noise variance; iterative texture
   rejection.
Evaluate both on synthetic noise with known σ (accuracy vs texture content) and choose.

**Where the profile lives:** stored in the effect's parameters (saved with the project, same result
on every machine and every render). Workflow: *Auto Profile* analyses the current frame and writes
the numbers; *Profile Mode: Auto per frame* is offered too but documented as possibly flickering.
**Research item R3:** how an effect can analyse a frame from a UI action in Premiere (AE API:
PF_Cmd_USER_CHANGED_PARAM + PF_CHECKOUT_PARAM of the layer? button parameter support?) and in OFX
(instanceChanged + fetching the source image at the current time). Verify against the SDKs; if not
possible in a host, use a manual persisted profile or deterministic estimation from a declared,
fixed input-frame window. Never smooth a profile over whichever frames happen to render: that
would make output depend on seek/render order. Persistent parameters are changed only in a
supported user-edit action, never ordinary rendering; caches must not change the result.

**Variance stabilisation (candidate):** compare a profile-derived stabilising transform with direct
covariance-aware filtering. Generalised Anscombe results apply to specific Poisson–Gaussian
models; they do not establish an exact unbiased inverse for arbitrary processed/log, correlated
video noise. Measure forward variance and inverse bias, including near black and after clipping.

## 3. Algorithm — Final mode (the "real deal")

Pipeline per output frame t, with neighbours t−k … t+k (k = 2 by default, up to 3):

1. **Pre-processing (candidate):** host frame → explicitly specified log/working-domain transform
   consistent with the profile → opponent space → optional validated variance stabilisation.
2. **Motion estimation** between t and each neighbour on a pilot-denoised luma (noise would
   otherwise dominate matching):
   - coarse-to-fine on the engine pyramid; block matching for large motion, then dense refinement
     (Lucas–Kanade / inverse-search style, à la DIS optical flow — Kroeger et al. 2016) to subpixel
     accuracy;
   - **reliability** per pixel: forward–backward consistency, warped-patch distance compared with
     the distance expected from noise alone, occlusion detection. A χ² interpretation needs a
     justified covariance model and calibrated false-accept/reject rates on held-out noise.
3. **Temporal fusion:** motion-compensated, patch-similarity-weighted average across the 2k+1
   frames (non-local means in time, weights from the χ² patch test), per band, so it reduces noise by
   up to √(2k+1) for equally weighted, independent, equal-variance, correctly aligned observations.
   Correlated noise, resampling and unequal weights change that limit. Reject unreliable
   neighbours and fall back to the spatial result; ghosting remains a risk to measure explicitly.
4. **Spatial stage**, two-step like BM3D / NL-Bayes (estimate → empirical Wiener):
   - pilot estimate from step 3;
   - candidates to evaluate (§6): (a) undecimated pyramid / wavelet-domain empirical Wiener with
     local signal variance from the pilot; (b) patch-based NL-Bayes (Lebrun, Buades, Morel 2013),
     and its space-time version (Arias, Morel 2018), a strong classical reference rather than an
     established universal leader. Include VBM3D and the 2025 hybrid filtering research in the
     comparison. Choose by measured quality, runtime and memory; do not assume (a) or (b) wins.
5. **Detail control:** per band strength (fine / medium / coarse / very coarse), separate luma and
   chroma amounts, *Detail Recovery* (re-inject a fraction of the high-frequency residual that the
   χ² test classifies as structure, not noise), optional light sharpening after denoising.
6. Inverse stabilisation → inverse opponent → host encoding → mix. Alpha untouched.

**Learned denoisers** (FastDVDnet etc.) are out of scope for v1: weights would need training data
and licensing we do not have, and an inference runtime on Metal/CUDA. Documented as a possible
later research item.

## 4. Preview mode and host integration

### 4.1 Preview mode
Same noise model and controls, cheaper search/refinement and spatial processing. No/one neighbour
and reduced-resolution chroma are candidates to measure, not fixed requirements. Keep colour and
control meaning consistent; measure Preview/Final differences rather than promising the difference
is only strength. Draft caches must never be reused as Final results.

### 4.2 Temporal access — research item R1 (blocking for the temporal part)
The effect needs frames t±k of its input:
- **Premiere, CPU (AE API):** PF_CHECKOUT_PARAM of the layer parameter at other times — verify
  that Premiere supports it and what it returns at clip edges.
- **Premiere, GPU:** PrGPUFilter frame dependencies (request input frames at other times) — verify
  the exact API in the GPU SDK headers and its behaviour.
- **OFX / Resolve:** temporal clip access (kOfxImageEffectPropTemporalClipAccess),
  getFramesNeeded, fetchImage at other times — verify Resolve's support.
Verification: a minimal probe in each host adapter, compiled in CI against the private SDK, plus a
user host test, before the temporal part is built on it. Verify that centre and neighbour frames
contain the same upstream processing. If a host refuses or supplies a mismatched input stage,
that path gets spatial-only operation or a narrowly verified workflow, explicitly documented.

### 4.3 Shared-engine changes
`vv::Effect` gains an optional temporal interface (frames needed for given parameters; render
with a list of neighbour images/GPU buffers). Additive: existing plugins unchanged, their tests
stay green.

### 4.4 Preview vs Final switch — research item R2
Parameter **Quality: Preview / Final / Auto**. *Auto* uses Final only when the host says it is a
final/high-quality render — research which signals exist (Premiere: render quality fields in
PF_InData / GPU render params; OFX: draft-render property in OFX 1.4) and whether Premiere and
Resolve set them during export. If no reliable signal exists in a host, Auto is not offered there
and the docs say "switch to Final before exporting" (the honest fallback). Default: Final;
explicit Preview remains Preview until changed. Verify exports and cache isolation in both modes.

## 5. Parameters (draft; disk IDs append-only once released; OFX prefix `dn`)
Preset · Master Enable · Mix · Quality (Preview / Final / Auto) · **Noise Profile** group (Auto
Profile action, Profile Mode, per-band/channel/intensity numbers hidden or collapsed, global
"Noise Level" scale) · **Luma** group (Amount, Fine/Medium/Coarse/Very Coarse) · **Chroma** group
(same) · **Temporal** group (Enable, Radius 1–3 frames, Motion Sensitivity) · **Detail** group
(Detail Recovery, Sharpen) · View (Result / Removed Noise / Motion Reliability) for checking.
Identifiers: Premiere match name `VVIS Denoise`, OFX id `com.venostavisuals.Denoise` — final once
released.

## 6. Evaluation (before choosing algorithms, and as regression tests)
- **Test material:** owned or expressly permitted material; redistribute only where permitted.
  Include the engine's synthetic scenes plus procedural
  textures (fine text, fabric, foliage-like noise, skin-like smooth gradients, edges at all
  angles), and synthetic **sequences** with known motion (pan, zoom, rotation, occluding object,
  motion blur) so ground truth exists.
- **Realistic noise synthesis:** signal-dependent (Poisson–Gaussian in linear light), Bayer
  mosaicking + simple demosaic (gives the spatial and chroma correlation), gamma, optional
  sharpening and block compression.
- **Metrics:** PSNR, SSIM (own implementation, independently validated), motion-aware error,
  disocclusion trails, detail/colour retention and noise spectra, plus blinded motion review at
  native scale. Noise-estimate accuracy and coverage for the profiler; protocol in RESEARCH_PLAN.md.
- **Baselines:** Gaussian blur, bilateral filter (ours, for reference), and offline reference runs
  of published BM3D / V-BM4D / NL-Bayes code where licences allow running them (results only;
  their code is not added to the repo). Add permitted learned and licensed commercial comparators
  under the domain-matched protocol in RESEARCH_PLAN.md; reference-use register in SOURCES.md.

## 7. Phases (each ends with tests + PROGRESS.md update)
- **D0** this plan + `docs/RESEARCH.md` (papers, maths, open questions R1–R3 resolved against the
  SDKs, host probes in CI).
- **D1** noise synthesis, metrics, evaluation harness (CLI), noise profile + estimator with accuracy
  tests.
- **D2** spatial denoiser candidate (a), CPU; evaluated.
- **D3** spatial candidate (b) NL-Bayes, CPU; evaluated; choose (or keep both: (a) Preview, (b) Final).
- **D4** motion estimation + reliability + temporal fusion, CPU; evaluated on synthetic sequences.
- **D5** shared-engine temporal interface + host adapters (Premiere CPU/GPU, OFX).
- **D6** GPU ports (Metal, CUDA) with emulation and Metal-vs-CPU tests; performance measured.
- **D7** presets, profile workflow, docs, installers, release as experimental; host tests by the
  owner.

## 8. Honesty rules
Report measured numbers only (quality metrics, timings with hardware). Never claim "real-time",
"better than X" or host behaviour without measurement or a host test.
