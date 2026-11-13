# Film Effects — Agent Development Instructions

> Authoritative project specification for coding agents (Codex, Claude Code, or similar).
> Project: **Film Effects** — a standalone, high-quality Adobe Premiere Pro video effect plugin.
> Date of specification: 2026-11-10.

## 0. Mission and scope

### Current product scope (owner decision, 2026-11-13)

Describe Film Effects as **free film finishing effects** for Premiere Pro and DaVinci Resolve:
Bloom, Halation, Film Grain, and optional Film Gate in one panel, applied to an existing grade.
Keep development focused on the current tools: image quality, usability, performance, and
reliability. Film-stock models, negative/print pipelines, camera LUT libraries, and printer-light
controls are outside the current scope. This decision and the shipped one-panel design supersede
earlier plans below for standalone effects and the original exclusion of Film Gate.

### Original mission

Build a **real, native, installable Premiere Pro video-effects plugin** that gives a colorist three film-finishing tools: **Bloom, Halation, and Film Grain**. The visual result should be competitive with professional film-finishing plugins, but the product is **not** a Dehancer clone and must not depend on or reproduce proprietary Dehancer technology, assets, names, interface, or algorithms.

The user's workflow is to grade individual clips with **Lumetri Color**, then apply a consistent stylistic finishing effect to those clips or, preferably, an **adjustment layer above them**. Film Effects should be the finishing stage: **Lumetri -> Film Effects** when the effects are arranged in that order. It must process the actual rendered Premiere image, respect all clips below it, and work in timeline playback and final exports.

This is **not** the separate "Track Range Exporter" project. Do not add clip export queues, track scanning, or batch-render functionality to this repository.

**Success means a plugin that loads in Premiere, renders pixels in Premiere, has working animatable parameters, survives project save/reopen, and produces visually good, consistent final exports.** A standalone Python demo, UXP mockup, LUT preset, After Effects template, or external render tool is *not* sufficient.

## 1. User priorities (in order)

1. Looks excellent at restrained, usable settings; no cheap glow or TV-static grain.
2. Native, straightforward Premiere experience and workflow alongside Lumetri.
3. Smooth playback and export, with Metal GPU acceleration on macOS as the first target.
4. Three effects that can be turned on/off and adjusted independently.
5. Predictable color, bit depth, opacity/alpha, keyframes, and deterministic renders.
6. Simple installation and practical presets.
7. Optional polished companion UXP panel **only after** the native effect works.

Avoid sprawling film-stock emulation, camera LUT libraries, printer lights, gate weave, film damage, standalone editor, or online services. Those are out of scope for version 1.

## 2. Target platforms and reality checks

- **Primary target:** macOS, Premiere Pro current installed version, Apple Silicon if available, **Metal** rendering.
- **Secondary/future target:** Windows with CUDA GPU acceleration where feasible; do not block macOS delivery waiting for Windows support.
- **CPU reference/fallback:** implement/test a mathematically equivalent CPU renderer when required by the relevant SDK/host path. Aim for close CPU/GPU visual agreement.
- Use **Adobe's current C++ Premiere Pro SDK / After Effects effect SDK with Premiere GPU extensions** for the actual pixel-processing native effect. Determine the correct supported sample and APIs from the SDK installed or obtained by the developer.
- Adobe's Premiere GPU extension documentation describes a software effect path plus GPU entry point, with native Metal support on macOS. The agent must check the version-specific SDK and examples before making assumptions about headers, PiPL registration, entry points, GPU surface access, or host color transforms.
- **Do not assume UXP alone can add a native pixel-rendering effect**. UXP is for an optional companion panel, preset controls, or inserting/managing a native effect. Do not claim to modify the internal Lumetri panel.
- First check actual machine architecture, macOS, Xcode, Adobe SDK, installed Premiere version, and whether Premiere GUI can be exercised from the environment. If missing, continue producing verifiable source/build/test artifacts; clearly mark host integration as unverified. Never report installation or playback as successful unless actually tested.

### Useful authoritative references (recheck during implementation)

- Adobe Premiere Pro developer landing / C++ SDK: https://developer.adobe.com/premiere-pro/
- Premiere GPU Effects & Transitions guide: https://ppro-plugins.docsforadobe.dev/gpu-effects-transitions/gpu-effects-transitions/
- Premiere GPU Metal/CUDA notes: https://ppro-plugins.docsforadobe.dev/gpu-effects-transitions/cuda-opencl-metal-opengl/
- Premiere Pro UXP: https://developer.adobe.com/premiere-pro/uxp/
- Adobe UXP Premiere samples: https://github.com/AdobeDocs/uxp-premiere-pro-samples
- Research only, possible reuse after careful license and compatibility review: https://github.com/LudwigKienle/buckswood-post-plugins (includes experimental native Premiere port)

Reference files in the Premiere C++ SDK such as `SDK_ProcAmp`, `Vignette`, and their GPU/Metal paths are preferable starting points when present. Review the current SDK's licensing/distribution requirements; don't paste proprietary Adobe SDK sources into the public repository unless permitted.

## 3. Required product design

### 3.1 Native plugin entry points

A combined native video effect shown as **Film Effects** in Premiere's Effects panel is **MVP mandatory**. It must expose three enable switches and parameter groups in Premiere's built-in Effect Controls, support default/undo/redo, and use standard keyframe/automation facilities to the extent the SDK supports them.

**Desirable follow-on:** standalone effects **Film Bloom**, **Film Halation**, and **Film Grain** that each can be used independently and also work in the combined Film Effects effect. Reuse exactly the same core algorithms for identical results. If separate effect packaging adds substantial host complexity, ship the combined effect first; track standalone effects as the next milestone. A user should not be forced to enable all three effects at once.

**Optional follow-on:** dockable UXP companion panel (TypeScript + supported Spectrum UI) with organized sliders, presets, toggles, and controls for the currently selected Film Effects instance. Verify each API in Adobe docs; if unsupported, the native Effect Controls UI remains fully functional. Panel must never be a prerequisite for rendering, recalling settings, or project portability.

### 3.2 Suggested controls — names are product requirements; numeric ranges are provisional

At minimum provide:

| Group | Control | Initial default | Intent |
| --- | --- | --- | --- |
| Global | Master Enable | On | Global bypass, pixel-identical when off |
| Global | Global Mix | 100% | Blend processed RGB against source |
| Bloom | Bloom Enable | Off or subtle preset | Independent toggle |
| Bloom | Intensity | modest, e.g. 0.15 on 0..1 scale | How much diffuse highlight light returns |
| Bloom | Threshold | user-adjustable | Luminance where bloom is activated |
| Bloom | Soft Knee | smooth transition | Avoid hard thresholds and artifacts |
| Bloom | Radius | adjustable in perceptual units/pixels | Spread of diffuse highlight glow |
| Halation | Halation Enable | Off or subtle preset | Independent toggle |
| Halation | Intensity | modest, e.g. 0.1 on 0..1 scale | Strength of red/orange scatter |
| Halation | Threshold | user-adjustable | Highlight detection |
| Halation | Radius | small to moderate | Extent of red/orange rim |
| Halation | Warmth / Hue | restrained | Red-to-orange bias, not a flat orange layer |
| Grain | Grain Enable | Off or subtle preset | Independent toggle |
| Grain | Amount | modest | Perceptual texture intensity |
| Grain | Size | physically plausible range | Effective granule scale / structure |
| Grain | Roughness | medium | Grain distribution/cluster frequency |
| Grain | Shadow / Midtone / Highlight Response | sensible curve, at least one response control | Luminance-dependent grain |
| Grain | Color Mode | Luma / subtle chroma | Neutral film grain, optional chroma |
| Grain | Seed | stable integer | Reproducibility |
| Grain | Animate | On | Time-varying grain frame by frame |
| Quality | Processing Quality | Normal / High if needed | Never silently change export appearance |

Optional later: bloom tint, halation mask visualization, grain-only preview, high-quality presets. Keep default panel approachable and use sensible internal physical units instead of arbitrary percentage mappings. Parameter labels must expose actual effect behavior clearly.

### 3.3 Presets

Implement a small set of presets with parameter values stored as readable data, not hard-coded proprietary LUTs: `Subtle Film`, `Soft Highlight Glow`, `Night Lights`, `Fine 35mm Grain`, `Coarse 16mm Grain`, `Halation Only`, and `Off`. A user can manually tweak any parameter afterward. Prefer native parameter recall and Premiere preset compatibility; if a custom preset format is necessary, document its location/versioning and avoid overwriting user values unexpectedly. Presets are later than the working renderer.

## 4. Visual algorithms and image-quality rules

The implementation should be independently developed from general image-processing and film-science principles. Existing open-source works may inform architecture or code **only after licensing review and attribution**.

### 4.1 Bloom — broad, natural optical diffusion

- Derive an appropriately color-managed, **soft-knee luminance mask** from high-intensity parts of the image; nonbinary response.
- Create smooth **multiscale** radiance diffusion (e.g. Gaussian pyramid/separable blur with multiple radii) to avoid a single flat blurry halo.
- Add/merge this energy conservatively, preferably preserving local color balance and perceptual highlight hierarchy. Do not turn midtone faces into glowing objects at subtle settings.
- Include threshold, knee, strength, and radius. Bloom can be broad and nearly neutral; distinguish it clearly from halation.
- Guard image edges (no dark borders), tiles/regions of interest, thin bright lines, resolution changes, and out-of-bounds memory. Define exactly how blur passes get a proper halo/padding when host renders subregions.
- Work with HDR scene-referred values without arbitrary 1.0 clipping. Avoid aggressive saturation or contrast shifts.

### 4.2 Halation — localized warm edge bleeding

- Simulate warm/red-biased scattering from **bright regions into adjacent darker surroundings**, not a uniform full-image red tint or red Gaussian glow.
- Use highlight detection and neighborhood/edge-aware weighting. Build and test a mask that preferentially creates a rim near high-contrast bright/dark boundaries and reduces uniform bright-area filling.
- Derive plausible red/orange spectral weighting from original pixel content, with user-adjustable intensity, radius, and warmth. Preserve natural skin, dark regions, and neutral highlights when disabled.
- In mixed bright signs against a dark background, show warm fringing close to the edges without giant, opaque orange blobs. Make radius relative to output resolution (or document a pixel-based choice) and keep 1080p/4K results visually coherent.
- Ensure halo/bloom compositing order is deliberate, documented, and visually reviewed. Do not apply twice in the combined/individual effect chain.

### 4.3 Film grain — organic, not just white noise

- Generate procedural, **spatially correlated / multiscale grain**, optionally via grain clusters, smooth filtering, and density modulation. A basic uniform per-pixel random noise layer is insufficient for a quality milestone.
- Grain **changes with each timeline frame** but is **deterministic** for the same input time, seed, settings, dimensions, and source image regardless of render order. Critical for scrubbing, Media Encoder exports, concurrent rendering, dropped frames, and previews; never depend on global mutable RNG state or wall-clock time.
- Derive temporal random inputs from stable frame-time information supplied by the host (e.g. frame timestamp or rational frame index, not an assumed constant 25/30 fps). Avoid frame collisions at fractional frame rates or during time remapping where feasible; document limitations.
- Support intensity, grain size, roughness, shadow/mid/high luminance response, luma vs subtle color grain, animation, and seed.
- Modulate by the image's signal/luminance/density: grain should interact with texture and tone, not behave as a fixed-opacity overlay on top of everything.
- At 100% viewer zoom, high quality grain should look film-like; avoid visible tiled patterns, seams, JPEG-like blocks, rainbow speckles, and frame-to-frame distracting color flicker.
- Preserve source details; offer sensible antialiasing and pixel-footprint policy for low-res previews vs final full-res exports. Decide and document whether grain adheres to full-frame output pixel coordinates or apparent physical scale.
- Avoid strong periodic patterns or reusing a small repeating noise tile. If using grain textures, ensure redistribution licenses and explicit ownership; procedural is preferred.

### 4.4 Color and pixel contract — critical

- Preserve color management: inspect documented host-provided pixel format, premultiplication status, transfer function, gamut, color metadata where available. **Do not assume all received pixels are sRGB or Rec.709 display-referred**.
- Perform radiance/light scattering (bloom/halation) in an appropriate linear-light representation wherever practicable; correctly map into/out of the host's pixel processing space, and be explicit if any mode is display-referred.
- Test scene-linear/high dynamic range and common SDR footage and Lumetri workflows. Handle negative and >1 floating values gracefully; avoid hard clipping except where specified.
- Support Premiere's 32-bit float processing where relevant. Handle supported 8-/16-bit formats accurately, with round-trip fidelity. Do not claim formats are supported without probing or implementing them.
- Never alter alpha accidentally. Respect straight/premultiplied alpha conventions, mask/transparent edges, and compositing over other video tracks. Do not generate invalid colors outside alpha-0 pixels.
- Respect frame dimensions, row stride, memory alignment, pixel aspect ratio, crop regions, and host-provided tiling/ROI. Handle cancellation, GPU loss, invalid parameters, empty frames, and clean resource release.
- **Full bypass condition:** with all effects off or master mix = 0, output must match input pixels within format-dependent exactness / quantization tolerance; no hidden color change.
- Do not apply a film stock look or LUT unless user explicitly chooses to add such a feature in a future version.

## 5. Rendering and performance architecture

1. Separate a tested, host-agnostic core from Premiere adapter code. Organize modules such as `core/bloom`, `core/halation`, `core/grain`, `core/color`, `core/params`, `hosts/premiere`, `gpu/metal`, `tests`.
2. Build **CPU reference kernels** with explicit IO pixel-format conventions so synthetic fixtures and image-quality checks run without Premiere.
3. Implement a Premiere native effect wrapper using a *supported Adobe SDK sample as a base*. Correctly register render callbacks and metadata, effect parameters, and (where supported) GPU device lifecycle. Test a color-preserving passthrough in host **before** effect algorithms.
4. **Metal GPU backend** for macOS. Keep input resident on GPU and avoid CPU readbacks when allowed by the current GPU extension API. Use an efficient blur (separable kernels/pyramids); reuse temporary textures carefully with valid synchronization. Avoid unnecessary allocations every frame and excessive GPU/CPU transfers.
5. Per-frame effect order should be documented, e.g. input -> bloom mask / bloom compose -> halation -> grain -> global mix. Change it only with a visual rationale and test comparison.
6. GPU kernels must not rely on undefined thread order, uninitialized buffers, or nondeterministic accumulation. Grain must be reproducible across re-renders; CPU/GPU approximate correspondence acceptable with defined tolerance.
7. Define performance benchmarks: 1080p25, 4K25, 4K30, 8-bit SDR and float HDR on actual hardware. Log median / p95 **per-frame render time**, peak memory footprint, and quality mode. Real-time 4K is an optimization goal, **not** a claim without measurement.
8. Graceful fallback if GPU capability unavailable; no silent black frames, broken UI, or crashes. Record actual capabilities and supported fallbacks in docs.
9. No telemetry, mandatory accounts, networking, subscriptions, or external dependencies during normal rendering.

## 6. Premiere workflow and UI requirements

- Effect appears in Premiere's normal **Effects** browser and can be dragged onto a clip or adjustment layer. The combined effect is not a separate standalone app.
- Parameters appear in **Effect Controls**, with grouped labels and meaningful values. Premiere native keyframing should work for animatable parameters. Disable toggles should actually skip work for that effect.
- User can stack it **after Lumetri Color** on the same clip, or place it on an adjustment layer above a Lumetri-graded timeline. Documentation must explain processing order and adjustment-layer behavior.
- Export must match playback for the same time/size/color configuration, within expected display/proxy/resampling differences. Do not bake preview-resolution grain into final resolution.
- Timeline edits, render cache invalidations, solo/mute tracks, speed changes, nested sequences, and project reopen should not corrupt settings or produce permanently cached inconsistent grain.
- UI should be legible in Premiere light/dark themes and avoid confusing duplicate settings. Do not recreate Lumetri's entire interface.
- If optional UXP panel is made, use supported Premiere UXP API to locate/add/select and set native effect parameters; **never claim it is physically embedded inside Lumetri**. It must be optional: no extra roundtrip and no link between project portability and a local panel config.
- Provide short, practical docs for installing/uninstalling the native plugin with Premiere restarted, where it appears, usage on clips/adjustment layers, rendering caveats, and how to update safely. Check platform-specific paths from the SDK rather than guessing.

## 7. Automated tests and visual review

Write actual tests before declaring milestones complete:

**CPU/core**
- Inactive/bypass input equals output; one effect active leaves the others unchanged as intended.
- Constant midgray/black/white; saturated colors; thin bright line; point light on black; bright window with dark frame; highlight gradient; chart/color checker; natural portrait; textured foliage; film footage.
- Small images and odd dimensions; arbitrary row strides; 1920x1080 and 3840x2160; correct output and no out-of-bounds reads.
- Threshold/knee monotonicity where appropriate, near-zero amount identity, maximum amount finite, no NaN/Inf, correct color/alpha behavior.
- Grain: same seed + time + settings -> identical pixels; different timeline frames -> different but statistically similar grain; rendering out of frame order -> identical final frame results; no periodic seams; expected response per luminance region.
- Bloom: center-point spread, uniform-highlight stability, multiscale kernel/energy behavior, no black border / tile boundary seam.
- Halation: bright/dark edge test confirms mostly warm spill around edge, not uniform frame red shift. Cyan/magenta/white lights reviewed.

**GPU and host**
- CPU/GPU reference difference within documented bounds; HDR/SDR and alpha test fixtures. Log tolerances rather than hide failures.
- 10+ minute timeline test with multiple Lumetri-graded clips and an adjustment layer using Film Effects; seek backward/forward, cache, reopen project, duplicate effects, render to file, and check full exported frames.
- A/B tests at 100% scale using 1080p and 4K stills, plus motion tests to judge flicker / grain consistency.
- Host can unload/reload plugin cleanly and survive cancellation, multiple simultaneous effects, parameter keyframes, and presets.

**Recommended reproducible artifacts**
- `tests/fixtures` with legally redistributable synthetic inputs; script to generate fixtures locally.
- `tests/reference-renders` or snapshot hashes (do not commit giant videos); include exact parameter metadata and tool versions.
- `docs/IMAGE_QUALITY.md` describing evaluation protocol and before/after stills; state clearly which comparisons are actual Premiere outputs.
- `docs/PERFORMANCE.md` with actual benchmark command/hardware/results and TODOs for untested items.

## 8. Development stages — follow this order strictly

### Stage 0: API research & platform audit
- Confirm host environment (architecture, OS, Premiere version, Adobe SDK, Xcode). Read the current C++ SDK and Metal GPU samples.
- Create `docs/FEASIBILITY.md`: available SDK versions, chosen extension approach, effect registration, host callback entry points, color/pixel formats, native parameter control, GPU API, plugin install path, any blockers.
- Identify and vet any open-source reference code for licenses and relevance. Do not choose Buckswood by default if its Premiere port is not actually buildable on the environment.

### Stage 1: Thin native Premiere plugin (proof first)
- Create a compiled **Film Effects** plugin with an enable toggle and one trivial passthrough/brightness parameter.
- Install/run in Premiere if available; verify that an adjustment layer affects everything below and that an export matches playback.
- Verify actual Metal GPU callback support/CPU fallback and diagnostic logging. Do **not** spend days making a polished UXP UI before this succeeds.
- If Premiere is not available to the agent, at least compile against a legitimate SDK, state inability to test the host, and provide exact instructions for the user to run/confirm.

### Stage 2: Pure CPU effect cores and tests
- Implement bloom, halation, and grain independently with suitable numerical / photographic tests.
- Produce a small CLI test renderer solely for development validation; it is **not** the final plugin.
- Build diagnostic imagery/metrics and visual A/B comparisons.

### Stage 3: Native Metal effects
- Port tested algorithms to Metal, optimize memory/blur paths, and compare against CPU reference.
- Connect full parameters, bypass, keyframes, color/alpha support to Premiere native effect.
- Validate rendered output at 1080p and 4K and resolve artifacts.

### Stage 4: Professional usability
- Combined UI with organized sections, reversible controls, stable defaults, quality options.
- Standalone effect entries reusing core (if viable), preset data, installation docs, sample `.prproj` (if license and compatibility permit).
- Perform in-host acceptance tests. Record honest performance and limitations.

### Stage 5: Optional UXP companion
- Only after Stage 4 is working; design a small dockable panel in TypeScript for one-click presets and grouped controls, if supported without fragile hacks.
- Test it controls native effect parameters and does not become a runtime dependency.

## 9. Definition of done and acceptance scenarios

### User scenario A — adjustment layer
1. Open Premiere with three clips on V1, different Lumetri grades on the clips, and one adjustment layer on V2.
2. Put Film Effects on the adjustment layer; enable Bloom, Halation, Grain.
3. Preview and export. Every clip receives **its own grade plus the shared film finish**; no precomposing, no After Effects, no export per frame.
4. Halation shows around bright edges, bloom diffuses highlights, grain looks natural rather than blocky noise.
5. Toggling any effect independently leaves the others working.

### User scenario B — single clip
1. Apply Lumetri to one clip and Film Effects after it.
2. Edit Bloom on a lamp, Halation around bright windows, and Grain Amount/Size.
3. Save and reopen project. Re-render same frame: identical grain for same seed and time; same parameter values.

### User scenario C — performance and quality
1. Run 1080p25 and 4K25 test clips, with GPU active where supported.
2. Report measured frame costs and differences between preview/export; do not claim universal realtime.
3. Verify transparency, color pipeline, and disabled-effect identity.

Project is **not done** merely when `npm run build` passes, a Python mock image looks good, or Premiere shows an empty panel. At minimum a native compiled effect plus host-tested working processing is needed to claim the plugin works.

## 10. Expected repository layout

Adapt to the SDK/build system, but preserve clear boundaries:

```
film-effects/
  AGENTS.md                  # This document
  CLAUDE.md                  # Claude entrypoint, imports this document
  README.md                  # Human installation and usage
  PROGRESS.md                # Stage, test results, blockers, next steps
  docs/
    FEASIBILITY.md
    ARCHITECTURE.md
    IMAGE_QUALITY.md
    PERFORMANCE.md
    LICENSING.md
    INSTALLATION.md
  src/
    core/
      bloom/
      halation/
      grain/
      color/
      params/
    gpu/metal/
    hosts/premiere/
    optional-uxp/            # Not part of MVP
  tests/
    fixtures/
    core/
    gpu/
    premiere-manual/
  scripts/
    build-macos.sh
    run-core-tests.sh
    install-dev-macos.sh
```

Do not check in vendor SDK binaries/headers without permission, compiled installers, machine-specific secrets, paid visual reference media, or gigantic intermediate renders.

## 11. Operating instructions for autonomous coding agents

- **Research → build a thin in-host proof → implement/test algorithms → accelerate/verify → improve UX.** Do not write only a speculative architecture document and stop.
- Prefer working source and tests to descriptions. Change code in manageable milestones. Keep `PROGRESS.md` current: checked/unchecked, exact commands run, observed results, issues and next actions. Keep logs concise and reproducible.
- Before a significant implementation choice, consult Adobe's current docs and installed headers/samples. Quote actual symbols and SDK versions in `FEASIBILITY.md`; **never fabricate native SDK interfaces or successful host tests**.
- Proactively use open-source sample code where appropriate, **but verify license, compatibility, origin, authorship, and actual source contents**, and include attributions. Use no proprietary Dehancer code or protected assets.
- Use clean C++ (C++17 or what current SDK supports), RAII for resource lifetimes, understandable Metal shaders, minimal dependencies, deterministic tests, and real error reporting.
- Stop and surface only genuinely blocking decisions (SDK unavailable, signing, Xcode, no machine to run Premiere, access restrictions). Otherwise make the simplest reasonable choice and continue.
- Never change user's Adobe preferences/projects without backups; don't perform destructive installation or overwrite unrelated plugins. Document manual installer steps for uncertain locations.
- On completion, provide: built/working artifacts if actually built, install instructions, screenshots/frames from **real** host tests if available, test reports, known limitations, performance on measured hardware, and prioritized next steps. If no native testing was possible, say so plainly.

## 12. First action after opening this repository

1. Read this entire document.
2. Inspect the working directory and development environment, including Adobe SDK availability.
3. Search the current Adobe docs and the sample implementations.
4. Write `docs/FEASIBILITY.md` with exact findings and start `PROGRESS.md`.
5. **Implement and compile the minimal native Film Effects passthrough plugin as the first proof**, before any companion UI or elaborate visual effects.
6. Continue through the milestones and do not stop after the plan unless a hard external dependency blocks execution.
