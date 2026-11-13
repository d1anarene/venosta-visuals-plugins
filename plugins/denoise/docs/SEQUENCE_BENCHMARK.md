# Sequence benchmark — first executable foundation

_2026-11-13. Research tool, not a denoiser or camera-profile validation._

## Scope and signal contract

`dn_sequence_tool` generates random-access procedural sequences with known clean references,
motion and visibility. Initial cases are `static-detail`, `subpixel-pan`, `moving-occluder`,
`hard-cut` and `exposure-step`. Every frame contains low-contrast skin-like texture, fabric,
foliage-like detail, hair-like lines, a slanted edge, dark gradients and flat coloured/exposure
patches. These are analytic patterns, not natural camera captures or physical skin/optics models.

The input is scene-linear RGB with Rec.709 primaries. Independent Gaussian noise with variance
`shotNoise * max(linear,0) + readNoise²` is added before encoding. Clean and noisy values then
use the [Academy's ACEScct scalar transfer equations](https://docs.acescentral.com/encodings/acescct/).
This applies the **curve only**: RGB is not converted to AP1, so it is **not full ACEScct colour**.
There is no Bayer mosaic, ISP, temporal correlation, quantisation, clipping or calibrated native
camera-log noise. Negative and above-range values are preserved. Exposure is controlled explicitly.

This set qualifies scoring machinery before real-log comparisons. It cannot establish quality
on S-Log, LogC, other camera encodings or real sensors. Native log curves/gamuts and owned captures
remain separate planned experiments in [RESEARCH_PLAN.md](RESEARCH_PLAN.md).

Motion fields are analytic **current-to-previous pixel displacements**, not estimated flow.
Visibility requires an in-bounds bilinear footprint on the same layer. Disocclusion scores cover
newly exposed background behind the moving object; they do not include image borders or cuts.
Cuts invalidate temporal visibility; full-frame error still reveals contamination at a cut.
Exposure changes retain geometry so temporal residual error can expose lighting lag.

## Build and qualify the metrics

From the repository root:

```bash
cmake -S plugins/denoise -B build-denoise -DCMAKE_BUILD_TYPE=Release
cmake --build build-denoise --config Release --parallel
ctest --test-dir build-denoise -C Release --output-on-failure
build-denoise/dn_sequence_tool benchmark --output /tmp/dn-controls.json
```

With a multi-configuration generator, executables are under `build-denoise/Release/`.
Python 3 enables the end-to-end CLI test; CMake prints its availability, and CI explicitly supplies
Python. CTest includes the existing profile tests, sequence geometry/log tests, metric qualification
and export/external-scoring/corruption tests. The CLI accepts explicit width, height, frame count,
exposure, noise, pan speed, rational frame rate and comma-separated seeds. Invalid/nonfinite
inputs fail. The small default (160×90, eight frames, three seeds) is a smoke run, not a resolution
or timing qualification. Use named hardware and larger cases for performance work.

Built-in controls are deliberately simple:

| Control | What it checks |
| --- | --- |
| `clean-oracle` | Exact truth: zero spatial/temporal residual, detail transfer one where defined; unattainable reference, not a method |
| `noisy` | Input noise/detail baseline |
| `blur-0.8` | Less noise can coexist with erased texture |
| `frozen-clean` | Temporal smoothness alone can reward a frozen/wrong image |
| `unaligned-temporal` | Averaging neighbours can create trails and contamination at cuts |

Reports contain every frame/seed/scenario, not a single favourable average. Control execution
time is measured separately from generation/scoring, and is **not** host, denoiser or end-to-end
performance. Do not compare such timing to published GPU throughput.

## Metrics and interpretation

All current scores use encoded RGB with equal channel weights; no luma coefficients, display
transform or range normalization are inferred. Errors in log and linear/display domains must not
be compared as though their units were interchangeable.

- **RGB RMSE and signed RGB bias** reveal image and colour/exposure errors.
- **Detail transfer** regresses output four-neighbour Laplacians on clean Laplacians. One means
  retained clean contrast; less than one indicates attenuation. Uncorrelated added noise does not
  systematically improve the expected regression, but individual samples fluctuate. Sharpening or
  signal-correlated errors can raise it above one: pair it with error and noise measurements.
- **Removed structure** is one minus detail transfer, without clipping. It is structure leakage,
  not the amount of random noise removed.
- **Detail regions** separately score skin-like texture, fabric, foliage, hair and moving
  foreground. Missing regions are `null` with zero pixels, not perfect scores. Detail centres
  require four valid masked neighbours to exclude patch boundaries from texture regression.
- **Flat residual standard deviation** is centred per RGB channel across the pooled flat patches.
  It includes tone-dependent deterministic error as well as random noise; it is not a calibrated
  pure-noise estimate. Later calibration must split exposure/colour patches and report bias.
- **Temporal residual RMSE** compares `(output-clean)` with the bilinearly warped previous
  `(output-clean)` on known-visible pixels. Separate disocclusion RMSE and full-frame errors
  prevent invalid motion masks from hiding trails/cuts. A wrong temporally stable output can still
  score well here, which is why spatial/detail metrics are mandatory companions.

Every metric carries its contributing pixel count. No history, cuts, empty regions and zero clean
detail energy yield `null`, never a silent zero. Interpolation changes residual-noise covariance;
do not compare temporal scores across motion cases without accounting for their different sampling.
The first version does not implement a perceptual ranking, independent SSIM cross-validation,
frequency-resolved texture transfer, uncertainty intervals or held-out real-camera evaluation.

## Export and external candidate scoring

Export a **new** directory; existing directories are not overwritten:

```bash
build-denoise/dn_sequence_tool export --scenario moving-occluder --seeds 7 --width 320 --height 180 --frames 12 --output /tmp/dn-dataset
```

Exports contain `clean-0000.pfm`, `noisy-0000.pfm`, backward flow, visibility/disocclusion,
detail/flat masks and detail-region labels for every frame. Float PFM is an interchange container,
not a colour-space tag: RGB float32, unit scale, bottom-up file rows, no alpha (fixtures are opaque).
Flow stores dx/dy in R/G; region labels are 0 outside detail and 1–5 for skin/fabric/foliage/hair/
foreground. `dataset.conf` is the strict versioned configuration; `manifest.json` is descriptive
metadata. `fingerprints.txt` binds image content and metadata for accidental-change detection.
FNV fingerprints are **not cryptographic**; retain original exports and trusted provenance.

Run a candidate on the noisy frames and place same-domain float RGB results in a separate directory
as `output-0000.pfm`, etc. Do not supply clean images or geometry to an ordinary blind candidate;
those files are for scoring. The clean-copy test in the suite is explicitly an oracle sanity check.

```bash
build-denoise/dn_sequence_tool score --dataset /tmp/dn-dataset --candidate-dir /tmp/dn-candidate --candidate-name my-method-v1 --candidate-settings "version/parameters/profile/temporal-radius/tile-overlap" --output /tmp/dn-score.json
```

Scoring uses the stored truth/masks, not a new reference synthesized by a newer binary. It rejects
missing, nonfinite, wrongly sized or corrupted data, binds configuration/manifest fingerprints,
and records dataset versus scorer source revisions/digests plus output fingerprints. Record model,
profile, input window, implementation version, hardware and transforms in candidate settings;
the tool cannot infer whether an external program used the correct domain or leaked clean truth.
Save the build/source snapshot with reports: a dirty Git revision alone is not a reproducible source.
The source digest covers Denoise runtime sources, the sequence tool, CMake and shared core/I/O sources.

## Host-ready float conversion and probes

The lossless PFM sequences can be converted to float32 EXR with an installed FFmpeg:

```bash
mkdir /tmp/dn-noisy-exr
ffmpeg -i /tmp/dn-dataset/noisy-%04d.pfm -c:v exr -pix_fmt gbrpf32le -format float /tmp/dn-noisy-exr/noisy-%04d.exr
```

Use a fresh destination and explicit colour management in the host; EXR does not make the stored
log-curve values linear. On the installed FFmpeg, one exported 320×180 frame passed a byte-exact
PFM → float32 EXR → PFM RGB roundtrip. This verifies that conversion locally, not every FFmpeg
version or a Premiere/Resolve import/export path. Lossy video or an unrecorded display transform
invalidates exact hash comparison.

Research probe build and actual-host matrix: [shared temporal probes](../../../shared/engine/probes/README.md).
For those tests, export with `--frame-marker 1` to add a visible decimal frame index. The marker
passes through the same exposure/noise/log transform but is excluded from detail/flat/motion
masks; the default quality benchmark has no marker. For example:

```bash
build-denoise/dn_sequence_tool export --scenario moving-occluder --frame-marker 1 --seeds 7 --width 640 --height 360 --frames 24 --fps-num 24000 --fps-den 1001 --output /tmp/dn-numbered-probe
```

Frame rate is rational playback metadata, not a claim about the host's source/sequence timing.
The probes show current/neighbour/difference inputs, log hashes/timing/quality and test a
user-action saved statistic. Their successful compile/load/kernel checks are separate from
Premiere/Resolve temporal-input and persistence verification, which is still unperformed.
