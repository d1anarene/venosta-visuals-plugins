# Feasibility audit (Stage 0)

_Date: 2026-11-10_

## Environment this was built in

| Item | Found |
| --- | --- |
| Machine | Linux container, x86-64, 4 vCPU (Intel Xeon 2.1 GHz) |
| macOS / Xcode / Metal compiler | **No** |
| Premiere Pro / After Effects | **No** (cannot be installed or run) |
| Adobe Premiere Pro C++ SDK / After Effects SDK | **No** — download requires an Adobe account; headers may not be committed |
| C++ toolchain | g++ 13.3, clang 18, CMake 3.28 |
| Network | Adobe developer sites blocked; GitHub reachable |

**Consequence:** the native plugin is written but **not compiled against the Adobe SDK and not
host-tested**. The host-independent core is compiled and tested here (`scripts/run-core-tests.sh`).
The Metal kernels and engine (no Adobe SDK needed) are compiled and verified against the CPU
reference on a GPU in GitHub Actions' macOS runners (`tests/gpu/README.md`).

## Sources read

- Premiere Pro SDK guide (source of ppro-plugins.docsforadobe.dev):
  github.com/docsforadobe/premiere-plugin-guide — `gpu-effects-transitions/*`,
  `universals/pixel-formats-and-color-spaces.md`, `intro/plugin-installation.md`.
- After Effects SDK guide: github.com/docsforadobe/after-effects-plugin-guide — `effect-basics/*`,
  `intro/pipl-resources.md`, `ppro/bigger-differences.md`, `ppro/unsupported-features.md`.
- Open-source reference: github.com/LudwigKienle/buckswood-post-plugins (MIT). Its Premiere port
  uses the legacy Premiere `xFilter` API (`PrSDKEffect.h`), CPU only, 32f BGRA. **Not used**: it
  has no GPU path and its film code is a different design. No code was copied.

## Chosen approach

1. **After Effects effect API hosted by Premiere** (`EffectMain`, `PF_Cmd_*`, PiPL). This is the
   path Adobe's GPU extensions are built on ("The GPU extensions work on top of effects and
   transitions built using the After Effects SDK"). It also makes the effect usable in After Effects.
2. **Premiere pixel formats** via the PF Pixel Format Suite in `PF_Cmd_GLOBAL_SETUP`:
   `AddSupportedPixelFormat(PrPixelFormat_BGRA_4444_32f)` and `..._BGRA_4444_8u` (both straight
   alpha). Premiere does not support AE's own high-bit path or SmartFX (`unsupported-features.md`).
3. **GPU**: `xGPUFilterEntry` / `PrGPUFilter` (`CreateInstance`, `DisposeInstance`, `Render`), Metal
   on macOS (Premiere's GPU architecture is "entirely CUDA/Metal"). Returning an error from
   instance creation makes Premiere use the software path for that instance (documented).
   GPU frames are `PrPixelFormat_GPU_BGRA_4444_32f` or `_16f` (per the SDK samples).
4. **Install path** (Premiere 22.0+, documented):
   `/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/` (macOS),
   `[Program Files]\Adobe\Common\Plug-ins\7.0\MediaCore\` (Windows).
5. **Parameters**: standard AE params (checkbox, float slider, popup, integer slider, topic groups)
   → Effect Controls, keyframes, undo and project save are handled by the host.
6. **Windows**: same AE-API effect built as `.aex`; PiPL converted with the SDK's `PiPLtool.exe`
   (the documented way: "PiPLs are compiled by processing a .r file through pipltool.exe").
   GPU: CUDA entry (`FilmEffectsGPU_CUDA.cpp`) with the CUDA runtime linked statically, as the
   guide recommends ("Statically Link to CUDA Runtime"). Built only when the CUDA toolkit is found;
   otherwise Premiere renders with the CPU path.

## Symbols used that are documented in the guides

`PF_Cmd_ABOUT/GLOBAL_SETUP/PARAMS_SETUP/RENDER/USER_CHANGED_PARAM`, `PF_OutFlag_DEEP_COLOR_AWARE`,
`PF_OutFlag2_FLOAT_COLOR_AWARE`, `PF_OutFlag2_SUPPORTS_THREADED_RENDERING`, `PF_ADD_CHECKBOXX`,
`PF_ADD_FLOAT_SLIDERX`, `PF_ADD_POPUPX`, `PF_ADD_TOPICX`, `PF_END_TOPIC`, `PF_ADD_SLIDER`,
`PF_ParamFlag_SUPERVISE`, `PF_ParamFlag_START_COLLAPSED`, `in_data->current_time / time_step`,
`AddSupportedPixelFormat`, `PrGPUFilterRenderParams::inClipTime / inRenderTicksPerFrame`,
`GetParam()`, `GetProperty()`, `xGPUFilterEntry`.

## Symbols taken from memory of Adobe's samples — verify against your SDK

These come from SDK_ProcAmp / Skeleton sample code, not from the guides:
`AEFX_SuiteScoper`, `PF_PixelFormatSuite1`/`kPFPixelFormatSuite`/`GetPixelFormat`,
`PF_WorldSuite2::PF_GetPixelFormat`, `PF_REGISTER_EFFECT_EXT2`, `PrGPUFilterBase`,
`PrGPUFilterModule<>`, `DECLARE_GPUFILTER_ENTRY`, `mDeviceInfo.outDeviceFramework`,
`outDeviceHandle`, `outCommandQueueHandle`, `mGPUDeviceSuite->GetGPUPPixData`,
`mPPixSuite->GetPixelFormat/GetBounds/GetRowBytes`, `PrPixelFormat_GPU_BGRA_4444_32f/_16f`,
`PrParam::mFloat64/mBool/mInt32`. They are isolated in `src/hosts/premiere/` (two files).

## Blockers

- **No Mac / SDK / Premiere here** → Stage 1 ("thin plugin compiled and run in Premiere") could not
  be completed by the agent. The minimal proof is to build with `scripts/build-macos.sh` and run
  `tests/premiere-manual/CHECKLIST.md` (first section: passthrough + toggles).

## Windows CUDA packaging (2026-11-11)

The v1.0.3 Windows release explicitly disabled `FE_WITH_CUDA`; Linux nvcc compilation did not
exercise the Premiere Windows GPU adapter. A separate Windows Server 2022 / Visual Studio 2022
job now enables `FE_WITH_CUDA=ON` with CUDA 12.9.1. The CPU-only build stays available. The user
reports a GTX 1080, so native Pascal `sm_61` code is explicitly included along with 75/86/89 and
forward PTX. CUDA 13 removes offline Pascal compilation; CUDA 12.x is intentional.

References checked: [NVIDIA CUDA 12.9 Windows installation guide](https://docs.nvidia.com/cuda/archive/12.9.1/cuda-installation-guide-microsoft-windows/index.html),
[CUDA 13 release notes](https://docs.nvidia.com/cuda/archive/13.0.0/cuda-toolkit-release-notes/index.html),
[toolkit installer action source](https://github.com/Jimver/cuda-toolkit/tree/b8bf9c6c28f8a92fbb04dcfcaee872e60c57462d).

## DaVinci Resolve status (2026-11-11)

No Resolve/OpenFX host adapter, build target, installer or tests exist in the checked repository
or available branches. All native host integration currently targets Premiere. No progress toward
an installable Resolve build is claimed; sharing image algorithms alone is not a Resolve plugin.
