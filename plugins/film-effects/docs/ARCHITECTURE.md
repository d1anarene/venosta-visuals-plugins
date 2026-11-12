# Architecture

```
src/core/            host-independent C++17, compiled and unit-tested everywhere
  common/fe_kernels.h  per-pixel maths shared VERBATIM by CPU and Metal (C++ and MSL)
  common/parallel.h    deterministic row-parallel loop
  params/              FilmParams, sanitising, built-in presets
  color/pixel_io       host buffers <-> straight-alpha RGBA float (8u/16u/32f, BGRA/ARGB, strides)
  blur/                Gaussian, [1 3 3 1] pyramid, blur *plans* shared with the GPU
  bloom/ halation/ grain/  the three effects
  pipeline/            process(): the effect order below
src/gpu/metal/       FilmEffects.metal (kernels) + MetalFilmEngine (encodes the same passes)
src/gpu/common/      per-pixel GPU pass functions + pass orchestration (C++ and CUDA)
src/gpu/cuda/        CUDA kernels (thin wrappers) + CudaFilmEngine (Windows/NVIDIA)
src/hosts/premiere/  AE-API effect (CPU entry), PiPL, Premiere GPU entry (Metal), CMake
                     FilmEffectsParams.h: one parameter table for all effect variants
tools/fe_render      development CLI (synthetic scenes, A/B images, benchmarks) - not the product
tests/core           unit/image tests (no dependencies)
```

## Effect variants

Only **Film Effects** (`FE_VARIANT` 0) is built and shipped. Until v1.0.11 the same sources were
also built as standalone Film Bloom / Film Halation / Film Grain (`FE_VARIANT` 1..3); the owner
chose one panel instead. The variant code is kept so its parameter tables stay tested. `FilmEffectsParams.h` holds one table of every control (stable on-disk IDs); each variant
lists the controls it shows. Controls a variant hides take their defaults, and a standalone
variant enables only its own effect, so it renders bit-identically to the combined effect with
only that section on (tested: `standalone_renders_identically_to_combined_section`).

## Processing order

1. **Bypass check** — Master off, Mix 0, or every effect off/zero → the host frame is copied
   unchanged (`PF_COPY` / blit). Bit-exact.
2. **Linearise** with the chosen *Working Space* (default Rec.709/gamma 2.4, sRGB, or Linear).
   Sign-preserving, no clipping, HDR values above 1.0 pass through.
3. **Bloom energy** from the input: soft-knee highlight weight × linear colour (chroma kept), blurred
   at 5 scales (6 in High quality) whose sigmas double up to *Radius* (% of frame height), weights
   growing toward the wide scales. Each scale is a normalised blur, so added energy is exactly
   `intensity × highlight energy`.
4. **Halation energy** from the input (not from bloom — nothing is applied twice): red-weighted
   highlight source, blurred at radius/2 and radius, then `spill = blurred − 0.92 × local source`
   so light lands mainly *outside* bright edges. Tinted deep-red→orange (*Warmth*).
5. Bloom + halation are added in linear light and re-encoded. Pixels with nothing added keep their
   original encoded value exactly.
6. **Grain** in the working encoding (perceptual domain): multiscale value-noise field (3 octaves,
   hashed lattice → no repeating tile), amplitude from a smooth shadow/mid/highlight response on the
   pixel's own luma, luma or colour mode.
7. **Film Gate** (optional, `src/core/gate`) on the finished film image (steps 3-6):
   dust/scratches are on the film, so they are placed in film coordinates and move with it;
   the film moves in the gate by the per-frame *weave* offset (sub-pixel, Catmull-Rom resampling,
   edge-clamped; vertical amplitude 1×, horizontal 0.6×, smooth drift + 30 % jitter); the lamp
   *flicker* is an exposure gain in linear light (amount 1 ≈ ±20 %); the *gate aperture* is a
   rounded rectangle with a soft edge, fixed to the frame, black outside. Defects darken (print)
   or lighten (negative) in linear light. Per-frame values come from the seed and frame key only;
   GPU backends compute the film image into a separate buffer, then run one gate pass.
8. **Global Mix** against the original; non-finite results fall back to the original pixel.
9. **Alpha** copied unchanged; fully transparent pixels are returned untouched.

## Conventions

- **Units**: bloom/halation radius in % of frame height → identical look at HD, 4K and in
  half-resolution previews. Grain size in pixels *of a 1080-line frame* → same apparent grain size
  at any resolution; grain coordinates are frame pixels, so a preview samples the same field.
  When granules become smaller than one render pixel (low-res preview) the amplitude is reduced
  in proportion (approximates the averaging the eye would see), instead of aliasing to white noise.
- **Time**: grain key = exact frame index (`time / frame duration`) when frame-aligned, otherwise a
  distinct key from both numbers (no collisions at fractional rates/time remaps). Seed and
  *Animate* are parameters. No RNG state, no wall clock → any frame renders identically in any order,
  on any thread, CPU or GPU.
- **Borders**: clamp-to-edge everywhere (no dark frame edges). Kernels reach 4 σ (no cut-off rings).
- **Alpha**: Premiere BGRA formats are straight alpha; AE ARGB formats are premultiplied and are
  un-/re-premultiplied around processing.
- **Determinism**: CPU threads split work by rows with no shared accumulation; Metal uses serial
  dispatch with fixed per-pixel loops.

## CUDA backend and how it is verified without an NVIDIA GPU

`src/gpu/common/fe_gpu_pixels.h` holds one function per GPU pass that computes a single output
pixel, exactly like one GPU thread; `fe_gpu_pipeline.h` decides which passes run with which
buffers (same sequence as the Metal engine). The CUDA kernels only compute (x, y) from the thread
index and call those functions. `tests/gpu/gpu_emulation_test.cpp` runs the same pipeline with a
CPU backend that calls the same functions pixel by pixel, in place, for 32f and 16f frames, and
compares with the CPU reference. CI also compiles the CUDA code with `nvcc`. What remains
unverified is CUDA-specific execution (device memory, launches, the Premiere GPU entry).

## CPU vs GPU agreement

Both backends use the same plan functions (`planBloom`, `planHalation`, `planGaussian`), the same
Gaussian weights (`gaussianKernel`) and the same per-pixel functions (`fe_kernels.h`). Remaining
differences: GPU `pow`/`exp` precision (compile with `-fno-fast-math`), half-float frames (16f), and
possible vertical orientation of the grain field if Premiere's GPU frames are stored bottom-up.
**Measured** in CI on a Metal GPU: < 0.001/255 for 32f frames and ≤ 0.062/255 for 16f frames across
all scenes and presets (`tests/gpu/README.md`). CUDA pass sequence (CPU emulation): identical for
32f, ≤ 0.062/255 for 16f. On real NVIDIA hardware small extra differences from GPU `powf`/`expf`
precision are expected (well within 1/255); to be measured.
