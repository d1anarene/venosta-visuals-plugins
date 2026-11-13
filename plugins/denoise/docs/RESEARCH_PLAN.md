# Denoise — research and validation plan

_2026-11-13. Owner priority: high-quality camera-log footage with visible sensor noise._

## Objective and present evidence

Aim for exceptional detail preservation and stable motion on camera-log footage, with practical
editing previews and a high-quality Final mode. Leadership is established by reproducible
comparisons in this target domain, not by algorithm names or a universal marketing claim.

We have a CPU noise simulator, profile estimators, a reconstructing pyramid and an initial synthetic
sequence benchmark with detail/motion metrics. Separate temporal research probes compile/load
locally. We do **not** yet have a denoiser, motion estimator, production temporal host adapter,
held-out real-camera benchmark or actual host test. D0 and D1 remain incomplete. Current measurements are in [RESEARCH.md](RESEARCH.md);
primary literature and source-use constraints are in [SOURCES.md](SOURCES.md).
Executable benchmark/probe instructions: [SEQUENCE_BENCHMARK.md](SEQUENCE_BENCHMARK.md).

## Product decisions and open decisions

- Primary input: high-quality log footage, including underexposed shadows and visible sensor
  noise. Begin with low-light faces, hair, fabric and foliage; keep compressed footage secondary.
- Prefer an early pipeline position before creative grading, sharpening and added film grain.
  Hosts can transform decoded source pixels before an effect sees them: verify the delivered
  transfer function, primaries, range, bit depth, alpha and sampling instead of assuming camera log.
- Never infer log encoding from image brightness. Use supported host metadata or explicit input
  selection. Compare filtering in native log with a documented working transform; preserve
  negative and above-range floating values without silent clipping or display tone mapping.
- Keep v1 filtering independently implemented and nonlearned. Learned methods are offline
  references only when terms permit. Revisit that boundary only if measured tradeoffs warrant it.
- Default to Final. Preview is explicit; no automatic export detection until demonstrated per host.
- Freeze neither the algorithm nor saved profile format before the experiments below.
- Progressive video is the initial research path. Field handling must be designed and tested
  before interlaced material is supported; do not blend fields as progressive neighbours.
- Host support for temporal filtering is conditional on successful input-stage probes. A spatial
  fallback must be visible and documented, not presented as equivalent to temporal Final.

## Work packages and decision gates

These are planned experiments, **not completed results**. Host probes and benchmark preparation
can proceed together. GPU production work follows a measured candidate selection.

| ID | Question / deliverable | Gate to proceed | Current state |
| --- | --- | --- | --- |
| E0 | Capture and benchmark manifest; fixed train/tune/test split; supported input contract | Every asset has provenance, permitted use, transform, range, noise/capture metadata and split; test set frozen before tuning | Synthetic float export/provenance implemented; real captures and split pending |
| E1 | Sequence generator, reference metrics, all-band profile reports, independent seeds | Metrics detect deliberate blur, frozen output, trails, colour shifts and injected profile failures; missing coverage is reported | Initial five-case generator, metrics/negative controls and external scoring implemented; full profiler reports/independent validation pending |
| E2 | Log-aware profiling: spatial, temporal, covariance and lowpass alternatives | Held-out accuracy, confidence and abstention measured by exposure, colour, band and motion; no hidden extrapolation | Known D1 limitations |
| E3 | Minimal temporal and profile-action probes for each host/path | Centre and neighbour pixels match the same upstream processing; timing, boundaries and persistence documented | OFX CPU / Premiere CPU+Metal compile/load and pixel contracts pass; actual host matrix unperformed; CUDA/OFX GPU missing |
| E4 | CPU spatial candidates: profile-aware pyramid Wiener versus patch Bayes | Quality/detail/noise curves against strong baselines; shift stability and compute/memory measured | No denoiser |
| E5 | Motion and temporal alternatives, including confidence calibration | Static, moving and occlusion performance separately pass; failed alignment safely reduces temporal contribution | No motion/fusion code |
| E6 | Preview/Final design and Metal/CUDA feasibility | Controls retain meaning; 1080p/4K latency and memory measured on named hardware; explicit quality cache separation | Not started |
| E7 | Production integration and release evidence | Random-order/parallel/export agreement, crash/cancel/device-loss checks and blinded real-footage comparison | Not started |

### E0 — material and capture protocol

Maintain three separate tracks; never combine their scores into an unexplained average:

1. **Known clean synthetic sequences.** Render subpixel-accurate textures, faces/skin-like ramps,
   thin text, edges, saturated colours, motion blur, transparency and independently moving layers.
   Generate clean references before adding calibrated sensor/ISP/log noise. Existing procedural
   stills are useful controls but are not a substitute for natural imagery.
2. **Owned or expressly permitted real camera-log captures.** Record camera/firmware, sensor mode,
   ISO/gain, exposure, white balance, lighting, codec, bit depth, subsampling, native gamut/log and
   all upstream denoising/sharpening. Disable such processing where possible; record unavoidable
   processing. Start with the owner's available camera; hold out other cameras/modes as acquired.
3. **Secondary stress material.** Compression, rescaling, chroma subsampling, fixed-pattern noise,
   hot pixels, banding, auto-exposure changes and prior temporal processing. Denoising does not
   recover clipped or codec-discarded information; identify unsupported cases rather than invent it.

For static references, capture repeated noisy frames of a locked scene and form an independent
high-SNR average with measured remaining noise. Exclude the entire temporal input window and every
other observation supplied to the tested method from its reference; shared noise biases scores.
Record the averaging domain and quantify residual bias: averaging log values is not equivalent
to averaging linear signals and then encoding, particularly in shadows and near clipping.
Use controlled exposure brackets to test shadow recovery. A brighter/lower-ISO take is useful
visual evidence but is not automatically pixel-aligned ground truth. For motion, use procedural
truth and any controlled repeatable-motion captures; natural unpaired video receives blinded
review, not invented PSNR.

Initial evaluation set target: at least 20 short sequences across skin, hair/fabric, foliage,
night/saturated lighting and gradients/graphics; include static, pan and occlusion cases. This
is an initial engineering set, not sufficient evidence for broad camera coverage. Use multiple
independent noise seeds per synthetic scene (initially at least five); split by scene and
camera/pipeline, not adjacent frames. Keep tuning and held-out material separate.

Each run records source/reference hashes, frame interval/rate, input/output transforms, range and
metric domain, camera/noise settings, seed, crop/border policy, software commit/build, hardware,
algorithm parameters, temporal radius, tiles/overlap, profile, runtime and memory. Keep licensed
footage outside git; commit metadata/results only where permitted. No external media downloaded
or permissions obtained as part of this research update.

### E1 — metrics that cannot be won by erasing detail

- Validate PSNR/SSIM against an independent implementation with identical windows, borders,
  peak/range and channel conventions. Current PSNR uses peak 1; current SSIM is luma-only.
  Do not reuse those settings silently for HDR or compare log-domain scores to display-domain ones.
- Report residual random-noise standard deviation, spatial spectrum and chroma covariance in
  known-flat regions, alongside structure leakage in the removed-noise view.
- Measure edge contrast/width, texture transfer, low-contrast skin/fabric detail and colour bias
  against clean references. Show crops and strength sweeps at **matched residual noise**.
- Let `e_t = output_t - clean_t`. Measure `e_t - warp(e_(t-1))` using known clean motion and
  visibility masks, paired with spatial error. Separately score disocclusion trailing and cuts;
  do not hide them by excluding every difficult pixel from the motion mask.
- Measure static noise, static bias, flicker, motion retention, one-pixel/subpixel shift consistency
  and coarse chroma blotches. A frozen frame, aggressive blur or weakened filtering must fail
  another reported metric even if it improves one temporal score.
- Report per-scene/region results, worst cases and paired uncertainty across sequences/seeds;
  frames from one shot are not independent samples. Include rejected/unsupported profile regions.

Fix tolerances and noninferiority margins on the tuning set **before** seeing held-out results.
The initial qualification target is to outperform blur/bilateral controls while preserving detail,
then close the gap to or improve on the strongest permitted reference for the chosen workflow.
No numerical leadership threshold is claimed before reference outputs exist.

### E2 — profiling and statistical experiments

Compare the existing profile with conditional RGB variance/covariance, spectrum-aware and
lowpass-residual models. Fit on separate frames from those used for validation. Test every band,
all populated and extrapolated bins, colour matrices, log curves, black-level offsets, shadows,
extended range, near-clipping values and changes in gain. Record pixel-weighted coverage and
confidence; abstention on texture-only or unsupported data is preferable to a confident bad profile.
The current shot-noise simulator uses a Gaussian approximation. Add exact Poisson sampling as a
low-count control and vary demosaic/matrix/transfer pipelines before drawing conclusions about
deep-shadow sensor noise; one bilinear camera pipeline is not sufficient calibration.

For temporal samples `a=s+n_a` and `b=s+n_b`,

`Var((a-b)/sqrt(2)) = (Var(n_a)+Var(n_b)-2 Cov(n_a,n_b))/2`.

The current normalization estimates one frame's variance only for equal-variance independent
noise and cancelled scene content. A fixed-pattern component cancels and can remain unmeasured.
Warping changes spatial covariance: for interpolation weights `w`, noise variance is `wᵀΣw`.
Subpixel registration error can leak detail into the estimated noise. Evaluate no alignment,
oracle alignment and estimated alignment separately; do not calibrate on oracle motion and
report that as achievable motion estimation.

Use full-frame pans/zooms/rotation, repeated patterns, independent foregrounds, occlusion,
defocus/motion blur, exposure/lighting changes, cuts, controlled temporal correlation and fixed
patterns. The current 16%-moving-block test leaves 84% static and does not establish these cases.
Compare direct covariance-aware filtering with variance stabilization; test inverse bias rather
than assuming arbitrary fitted transformations have an exact unbiased inverse.

Profile storage must include model version and domain/scale information. Locks must survive
save/reopen, copy/paste, undo/redo and another machine. A changed upstream transform or proxy
scale must invalidate or explicitly remap the profile. No result may depend on render history.

### E3 — host probes before temporal integration

The same-stage requirement is blocking: raw neighbours cannot be blended with an already
processed centre frame. SDK access alone is insufficient. See [SOURCES.md](SOURCES.md#host-integration).

| Probe | Required observations |
| --- | --- |
| Premiere CPU / GPU and Resolve OFX, separately | Declare/fetch neighbours; save numbered inputs and hashes; verify requested/delivered time, dimensions, format, scale and upstream stage |
| Bare clip, upstream colour/geometry effect, two instances, nested clip, adjustment layer | Compare each neighbour against independently rendered expected upstream pixels; do not recurse into this effect's own output |
| Clip/source boundaries, trims, short clips, cuts/dissolves, missing/repeated frames | Explicit valid-neighbour masks; reject duplicates as independent evidence; reset at cuts, renormalize valid weights |
| 23.976/25/29.97/50/59.94, mixed rates, freeze, 50%/200%/reverse/time remap | Rational source/sequence timing and identity; no silent rounding or use of a wrong shot |
| Playback, scrubbing, thumbnails, preview-cache generation, export and Media Encoder | Record quality/draft/interactive/scale signals; prove Auto if possible; default Final otherwise |
| Sequential, reverse, shuffled, parallel, warm/cold cache, project reopened | Identical results for the same input state; caches are optional optimizations |
| Profile action | Supported user-edit callback; one undoable parameter update; no persistent writes during ordinary render |

Current shared adapters are spatial-only. Premiere GPU consumes `inFrames[0]`; OFX disables
temporal effect/source flags. Extend the shared engine additively after a probe establishes the
contract, keeping all existing plugin tests green. Release OFX image handles within the action;
only owned copies may enter a bounded cache. If a path cannot supply same-stage neighbours,
document spatial-only operation or a narrowly verified placement instead of silently mixing stages.

### E4/E5 — candidate selection and ablations

Compare:

- Decimated, undecimated and optionally cycle-spun profile-aware pyramid Wiener variants, with
  one-pixel/subpixel shift tests and measured memory cost.
- Two-stage patch Bayes with covariance-aware matching/shrinkage; assess VBM3D/VNLB-style grouping
  as references rather than assuming temporal averaging followed by spatial denoising is optimal.
- Block matching versus dense refinement; pilot-denoised versus noisy matching; direct patch
  grouping versus warping all neighbours; radius 0/1/2/3; no confidence versus calibrated confidence.
- Spatial-only, temporal-only and combined results; current versus improved noise model; with and
  without variance stabilization; detail recovery and sharpening off for the core comparison.

Failure cases include repeated textures, thin moving hair, out-of-focus edges, strong sensor noise,
rolling shutter, illumination changes and motion blur. Confidence masks are fallible; use
forward/back consistency, visibility and noise-aware residuals but validate their error rates.
Detect shot boundaries and unreliable neighbours explicitly. Rejecting all neighbours must yield
a useful spatial result. No "ghost-free by construction" guarantee is justified.

Candidates advance only with reproducible reference comparisons, per-case failures and
quality–runtime–memory curves. Keep a small number of justified candidates; do not build a large
feature panel before choosing a filter.

### E6/E7 — performance, previews and product qualification

Benchmark 1080p and UHD at 24/25 fps, then 50/60 fps stress cases. Name the exact Apple Silicon
and NVIDIA machines, memory, OS/driver and host version before setting performance gates.
The earlier GTX 1080 target is a compatibility/performance tier to verify, not a measured result.
Record cold and warm wall-clock p50/p95, analysis/flow/filter/transfer time, peak CPU/GPU memory,
concurrent instances, cache footprint and exports. Do not present kernel-only time as host speed.

Seven UHD RGBA float32 input frames alone occupy about **886 MiB**; flow, pyramids, profiles,
intermediates and host allocations add more. Plan bounded frame/tile caches and halo/overlap
rules; compare tiled and untiled outputs for seams. Cost includes preparing temporal neighbours.
Profile image-scale changes explicitly; proxy noise is not the original sensor noise resized by
a simple strength multiplier. Cancellation/device-loss fallback must retain the selected quality
or report inability; it must not quietly render Preview as Final.

Preview should retain colour, profile and control meaning while reducing search/refinement cost.
Measure the difference rather than promising identical appearance or merely "less strength".
Use quality and all content-affecting state in cache keys; prove Final exports cannot reuse draft
results. A single frame result must not require a previously rendered frame or a warm cache.

Compare against licensed Neat Video and Resolve Studio on identical eligible footage, documented
versions/hardware, competent profiling/tuning and matched residual noise; use strength sweeps,
not one favourable preset. Record default and reasonably tuned results and setup time. No purchase,
download or commercial comparison has been performed in this update.

Use randomized, blinded A/B moving clips at native scale with the same display transform and
uncompressed/lossless comparison outputs. Ask reviewers to assess remaining noise, skin/texture,
trails, flicker, colour and overall preference separately. Record disagreements and difficult shots.
Any claim must name the tested domain and tradeoffs; a paper score or synthetic win is insufficient.

## Immediate execution order

1. E0/E1: freeze the log-input/capture manifest, sequence tests, metric conventions and reference
   runner records. Existing D1 tests remain a small regression suite, not a quality benchmark.
2. In parallel, E3: build tiny host probes using the shared host framework and legitimate SDKs;
   collect actual host evidence before production temporal adapter changes.
3. E2: expose all-band profile coverage and confidence; fix colour/log/coarse-band deficiencies
   based on held-out experiments. Do not begin by adding a denoising strength slider to bad estimates.
4. E4/E5: compare spatial candidates, then motion/temporal designs with ablations and offline
   references. Select the design from the evidence.
5. E6/E7: port/optimize, validate Preview/Final and host/export behaviour, then run the real-footage
   comparison. Quality, determinism and safe integration are release gates.

This is a preparation plan. Outstanding captures, host probes, baseline runs and algorithm
experiments remain work to do; none is marked complete because its procedure is documented.
