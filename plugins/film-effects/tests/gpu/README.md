# GPU (Metal) verification — to do on a Mac

The Metal backend (`src/gpu/metal`) is not compiled yet. Planned harness:

1. Build `MetalFilmEngine` into a small macOS command-line tool (`tests/gpu/metal_vs_cpu.mm`, to be
   written on the Mac): load each synthetic scene from `tools/fe_render`, upload it as a BGRA float
   buffer, run `MetalFilmEngine::encode`, read back, and run `fe::process` on the CPU copy.
2. Report max/mean absolute difference per channel for every preset at 1920×1080 and 3840×2160,
   for 32f and 16f frames.
3. Tolerances to start with: SDR output |Δ| ≤ 2/255; bloom/halation energy within 0.5 % relative;
   grain identical up to float rounding (same hash and lattice maths).
4. Log the numbers in `PROGRESS.md`; do not loosen tolerances silently.
