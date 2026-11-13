# Temporal host probes — research only

These are separately identified, opt-in diagnostics for testing **which input** a temporal effect
actually receives. They do not denoise, install themselves or modify the production adapters.
No probe is part of release.yml. Do not use probe effects in production projects.

## Implemented and verified

| Path | Implemented | Local verification (2026-11-13) | Still unverified |
| --- | --- | --- | --- |
| OpenFX CPU | Temporal effect/source flags, frames-needed range, neighbour fetch, current/neighbour/difference display, analysis action | Compiled against pinned OpenFX; binary loaded and entry points checked | Resolve temporal behaviour and profile action; no real or fake OFX host render has run |
| Premiere CPU | Wide-time input, layer checkout at signed frame offsets, separate format decoding, analysis button | Compiled against private Adobe 27.0 headers on macOS arm64; load/symbol check passes | Premiere input-stage consistency, timing/boundaries, UI action, save/undo/export |
| Premiere Metal | GPU dependencies at sequence time; GPU float16/32 display; input readback hashes | Compiled against private Adobe 27.0 headers; load/symbol check passes; six Metal pixel comparisons pass on the Mac GPU | Actual Premiere GPU frame delivery and resource/queue behaviour |
| Premiere Windows GPU | Not implemented | None | CUDA dependency/render probe remains work |
| OpenFX GPU / Windows Arm | Not implemented/tested here | None | CPU OFX observations must not be treated as GPU proof |

Common pixel tests cover identity, neighbour/difference modes, alpha preservation, hashes,
missing/mismatched input rejection and order independence. Metal pixel tests use odd dimensions,
different padded strides, float16/32 and negative/above-one values; these are **kernel** tests,
not an Adobe host test. If no Metal device is available CTest explicitly skips that test.
The local unprivileged run skipped GPU access; a separate authorized GPU run passed all six cases.

## Build

Run from the repository root. Default build needs no SDK and builds diagnostic contracts/tests:

```bash
cmake -S shared/engine/probes -B build-probes -DCMAKE_BUILD_TYPE=Release
cmake --build build-probes --config Release --parallel
ctest --test-dir build-probes -C Release --output-on-failure
```

For OpenFX add `-DVV_PROBE_OFX=ON`. Headers/support sources are fetched from the same pinned
BSD-3-Clause OpenFX revision as the engine, or pass `-DOFX_SDK_DIR=/path/to/openfx` to build
without network access. Output is `build-probes/VVTemporalProbe.ofx.bundle`.

For Premiere add `-DVV_PROBE_PREMIERE=ON -DAE_SDK_DIR=/path/to/AfterEffectsSDK/Examples
-DPR_SDK_DIR=/path/to/PremiereSDK`. On macOS, add `-DVV_PROBE_METAL=ON` to build the independent
Metal probe and its pixel comparison test. The legitimate private Adobe headers/samples remain
outside this repository. Binaries are `TemporalProbeCPU.plugin` / `TemporalProbeMetal.plugin`
on macOS, or `Release/TemporalProbeCPU.aex` with a Windows multiconfiguration generator.
The opt-in `temporal-probes.yml` workflow performs cross-platform builds, load checks and tests;
CI completion is not claimed until that workflow actually runs.

Local SDK source: existing private `d1anarene/vvp-sdk` revision
`77fc9bbae16487a2921fd969f7a2329b8a1b6e9f`, Adobe 27.0 macOS packages; temporary extraction only.
OpenFX revision: `4249fe1b590a7e65caf4b1e4693b6b53220703f5`.

## Controls and evidence

- **Neighbour offset**: −3…+3 frames; default −1. Offset **0** is a particularly useful stage test:
  fetched input at current time should match centre input if their upstream stages are identical.
- **View**: 0 = current, 1 = neighbour, 2 = absolute RGB difference × gain. Source-centre alpha is
  preserved. Current view requests the neighbour too, but can display current if the host provides
  none; missing data is logged. Neighbour/difference views fail rather than silently substituting
  the current input. Size/format/bounds discrepancies also fail those modes.
- **Log diagnostics**: off by default. Writes one JSONL record per action/render to stderr, or
  appends to an explicitly configured `VV_TEMPORAL_PROBE_LOG` path. No source pixels are written;
  logs contain input hashes, times, sizes, quality/scale and path. Files stop receiving records at
  16 MiB; use a fresh log for each case. Logging can be expensive and is not a performance test.
- **Measure current input**: fetches source during a user action and persists a simple finite-RGB
  mean in a normal parameter. This is a callback/persistence probe, **not a noise profiler**.
  Ordinary rendering never writes saved profile parameters. Premiere's analysis checkout can
  itself differ from centre processing; compare its hash against a current-view render.

The hashes are FNV-1a over dimensions plus canonical little-endian float32 RGBA pixels in top-row
order, excluding padding. Hashes are useful identities, not cryptographic evidence. Native formats
are decoded before hashing; compare captures from the same scale/format/context. Current hashes
include alpha, so an upstream alpha change is detected too. The logs record **requested** times:
these APIs do not independently prove the returned content time. All records explicitly say
`returned_time_verified:false`. Use visible frame numbers and independently rendered references.

For a complete capture of one host case, compare fetched hashes with separately rendered current
frames using:

```bash
python3 shared/engine/probes/check_probe_log.py /path/to/case.jsonl
```

A missing reference, unavailable neighbour, inconsistent current hash, mismatched dimensions/bounds
or hash mismatch exits nonzero.
Zero only means hash consistency for the captured inputs; repeated/held frames can still hide a
wrong delivered time. Never mix unrelated projects/cases in one log or label this check as a host
certification. Current frames at the requested neighbour times must be included in the capture.

## Manual host protocol (not yet performed)

1. Use a disposable project and numbered float test frames with known motion and colour bars.
   Back up any existing test project before changing it. Record app/build, OS/GPU/driver,
   renderer, project/input colour settings, sequence/source rate and probe build commit.
2. Install only the research bundle into the host's documented plugin folder, with the host quit;
   restart. The group is **Venosta Visuals Research**. Do not replace production plugin bundles.
   No automatic install or application-preference changes are performed by this repository.
3. For every case below, save independent current-view reference frames at all required times,
   then capture neighbour and difference outputs plus one new JSONL log. For raw-log tests,
   document host colour transformations; synthetic float log output does not establish native
   camera-log delivery. Use lossless float outputs for comparisons, not screenshots alone.
4. First test offset 0 on bare input, then after a conspicuous upstream colour adjustment and
   upstream geometric transform. A mismatch here exposes raw-source versus preceding-effects
   checkout. Do not accept later temporal results until this is understood per path.
5. Test offsets −1/+1 and radius boundaries −3/+3. Independently inspect visible frame numbers,
   hashes and clean expected upstream renders. A successful checkout does not establish the
   correct time, correct shot, matching source stage or GPU acceleration.
6. Run the matrix, retaining failures and missing evidence. Have a separate result record for
   each host/path; CPU checkout cannot validate GPU delivery or Resolve.

| Case | What must be observed |
| --- | --- |
| Bare clip; upstream colour and transform; two probe instances | Same-stage centre/neighbour content; no self-output recursion |
| Nested clip; adjustment layer above two shots; cut and dissolve | Which composite/source is fetched; no blending across unrelated shots |
| Trimmed and source edges, 1–3 frame clips, missing/repeated source frames | Host clamping/repetition/failure is visible; repeated images are not independent noise samples |
| 23.976/25/29.97/50/59.94 fps; mixed-rate footage | Requested versus visible source/sequence time, scale, pixel aspect and sampling |
| Freeze, reverse, 50%/200%, variable time remapping | Source identity; no assumption that sequence-neighbour is source-neighbour |
| Scrub, full playback, preview-cache generation, export, Media Encoder | Quality/draft/scale fields; render caches; no claimed automatic export signal without evidence |
| Chronological, reverse, shuffled, concurrent, cold/warm cache | Identical captured inputs and output for the same state |
| Measure → save/reopen, copy/paste, undo/redo | One user-action update persists; rendering does not mutate the saved mean |
| Half/full resolution and upstream colour-management changes | Which domain and sampling reach profiling; no reuse of an incompatible profile |

Interlaced material, OpenFX GPU and Windows CUDA are not covered by these initial probes.
If CPU checkout supplies raw neighbours while the centre includes upstream effects, mark that
workflow unsupported for temporal filtering or constrain placement based on verified evidence.
No denoising should be built on a failing stage contract.

## Result record

Store host results outside source control unless all media permissions permit sharing. A result
must identify case, app/path/build, input/reference hashes and settings, requested offsets,
visible delivered frame numbers, available/missing neighbours, same-stage verdict, quality/cache
observations, profile persistence, logs/output locations and reviewer. Use **pass / fail / untested**;
never turn a successful compile, binary load, pixel kernel or synthetic mock into a host pass.
