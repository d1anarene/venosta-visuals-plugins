# Venosta Visuals shared engine

Common code for every plugin in this repository, so each plugin only contains its own effect.
Owner decision (2026-11-12): shared engine instead of copying code between plugins.

| Part | Path | Status |
| --- | --- | --- |
| Core: image types, deterministic row threading, Gaussian blur + 2x pyramid, blur planning, host pixel-format conversion | `core/vv/` (`vv_core` library, namespace `vv`) | in use (Film Effects) |
| Per-pixel maths valid in C++, CUDA and Metal: transfer functions, luma, hashing, value noise, Catmull-Rom | `core/vv/kernels.h` (`vv_` prefix) | in use (Film Effects) |
| GPU framework (CUDA/Metal helpers, pyramid passes) | — | planned |
| Premiere Pro (AE API + GPU entry) and OpenFX (DaVinci Resolve) adapters driven by a parameter table | — | planned |
| Installer and CI templates | — | planned |

Use from a plugin's `CMakeLists.txt`:

```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/engine ${CMAKE_BINARY_DIR}/vv-engine)
target_link_libraries(my_core PUBLIC vv_core)
```

Metal shaders that include `vv/kernels.h` need `-I <repo>/shared/engine/core`.

Rules: no Adobe SDK files here (repository rule 4); every change must keep all plugins' tests
green; keep the maths deterministic (no RNG state, no wall clock, fixed evaluation order).
