# GPU (Metal) verification

`metal_vs_cpu.mm` renders every synthetic scene with every preset through `MetalFilmEngine` (32-bit
and 16-bit float frames) and through the CPU reference, then compares the results in display terms
(clamped to 0..1). It runs in CI on `macos-latest` (job `film-effects-metal`).

Build and run on any Mac with Xcode:
```bash
cmake -S . -B build-gpu -DFE_BUILD_GPU_TESTS=ON && cmake --build build-gpu
./build-gpu/fe_metal_vs_cpu build-gpu/FilmEffects.metallib
```

Tolerances: 32f max 2/255, mean 0.1/255; 16f max 4/255, mean 0.5/255.

## Result (2026-11-10, GitHub Actions run 42899553518, "Apple Paravirtual device")

48 scene/preset combinations × 2 formats: **all pass**.
- 32f frames: max difference < 0.001/255 everywhere (same maths header on both sides).
- 16f frames: max ≤ 0.062/255, mean ≤ 0.019/255 (half-float storage of input/output).

Not covered here: Premiere's own GPU entry point and frame handling (`FilmEffectsGPU.mm`), real
Apple Silicon/AMD GPUs, and GPU performance — those need a Mac with Premiere.

## CUDA (Windows / NVIDIA)

`gpu_emulation_test.cpp` (built and run with the core tests on every platform) runs the CUDA
backend's exact pass sequence and per-pixel functions on the CPU, in place, for 32f and 16f frames,
and compares with the CPU reference: **identical for 32f, ≤ 0.062/255 for 16f** (48 scene/preset
combinations each). It also checks the portable half-float conversion against the compiler's
`_Float16` for all 65,536 values. CI (`film-effects-cuda`) compiles the CUDA kernels and engine with
`nvcc`. Running on a real NVIDIA GPU is still to do: a `cuda_vs_cpu` tool like `metal_vs_cpu.mm`
can be added once a machine with an NVIDIA GPU is available.
