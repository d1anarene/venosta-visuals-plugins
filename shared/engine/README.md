# Venosta Visuals shared engine

Common code for every plugin in this repository, so each plugin only contains its own effect.
Owner decision (2026-11-12): shared engine instead of copying code between plugins.

| Part | Path | Status |
| --- | --- | --- |
| Core: image types, deterministic row threading, Gaussian blur + 2x pyramid, blur planning, host pixel-format conversion | `core/vv/` (`vv_core` library, namespace `vv`) | in use (Film Effects) |
| Per-pixel maths valid in C++, CUDA and Metal: transfer functions, luma, hashing, value noise, Catmull-Rom | `core/vv/kernels.h` (`vv_` prefix) | in use (Film Effects) |
| Plugin interface: `vv::Effect` (info, parameter table, CPU render, presets via `paramChanged`, GPU renderers) | `core/vv/effect.h`, `core/vv/params.h` | in use (Film Effects) |
| Premiere Pro adapter: AE effect API (registration, parameters, pixel formats, bypass), GPU entry for Metal and CUDA, PiPL template | `hosts/premiere/`, `cmake/vv_premiere.cmake` | in use (Film Effects) |
| OpenFX adapter (DaVinci Resolve): parameters, presets, CPU + CUDA/Metal render, bundle layout incl. Win-arm64ec | `hosts/ofx/`, `cmake/vv_ofx.cmake` | in use (Film Effects) |
| Metal shader compile helper | `cmake/vv_metal.cmake` | in use |
| Load tests (`.aex` LoadLibrary, `.ofx` dlopen + identifier check) | `tests/` | in use |
| Scalar ACEScct transfer curve (no gamut conversion), lossless float RGB PFM interchange | `core/vv/log_transfer.h`, `io/pfm.h` | research benchmark support; explicit domain required |
| Independent temporal input/profile-action research probes and log checker | [`probes/`](probes/README.md) | OFX CPU / Premiere CPU+Metal compiled locally; actual host verification pending; outside release builds |
| GPU framework: per-pixel pyramid/blur passes and half floats (C++/CUDA), `vv::gpu::Pyramid` | `gpu/` | in use (Film Effects) |
| CUDA backend base for the generic passes (`vv_cuda_backend.cuh`), Metal backend (`MetalBackend`, `vv_common.metal`), CPU emulation backend for tests (`vv_gpu_emulation.h`), x-only pyramid | `gpu/cuda/`, `gpu/metal/`, `gpu/` | in use (Lens & Diffusion); Film Effects still uses its own Metal engine |
| Horizontal-only blur pyramid, synthetic test scenes | `core/vv/blur.h`, `core/vv/scenes.h` | in use |
| Installer templates: Windows (Inno Setup) for Premiere and Resolve, macOS `.pkg` scripts, installer test | `installer/` | in use (Film Effects, Lens & Diffusion) |
| CI templates: `.github/workflows/vv-premiere.yml`, `vv-ofx.yml` (build, load tests, installers, installer tests on x64 and Arm) | repository `.github/workflows` | in use |


A new plugin implements `vv::Effect` once (and `vv::effect()`), writes a two-line PiPL file
that defines `VV_PIPL_NAME`, `VV_PIPL_MATCH`, `VV_PIPL_CATEGORY` and includes `vv_pipl.r`, and
calls `vv_add_premiere_effect(...)` / `vv_add_ofx_bundle(...)`. Saved-project identifiers
(Premiere match name, parameter disk IDs, OpenFX id and parameter prefix) must never change.

Use from a plugin's `CMakeLists.txt`:

```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/engine ${CMAKE_BINARY_DIR}/vv-engine)
target_link_libraries(my_core PUBLIC vv_core)
```

Metal shaders that include `vv/kernels.h` need `-I <repo>/shared/engine/core`.

Rules: no Adobe SDK files here (repository rule 4); every change must keep all plugins' tests
green; keep the maths deterministic (no RNG state, no wall clock, fixed evaluation order).

## Symbol export rule

Plug-in binaries export **only** their host entry points (OpenFX: `OfxGetNumberOfPlugins`,
`OfxGetPlugin`; Premiere: `EffectMain`, `PluginDataEntryFunction2`, `xGPUFilterEntry`) via
`cmake/vv_exports.cmake`. Every plug-in contains the same engine and OpenFX support code; when
those symbols were exported (up to v1.0.16), a host loading several of our plug-ins could bind one
plug-in to another's copy (macOS merges exported weak C++ symbols across loaded images), mixing
code from different releases. `tests/ofx_load_test.cpp` fails if internal symbols are visible.
