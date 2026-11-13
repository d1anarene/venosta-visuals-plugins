# Denoise (Premiere Pro + DaVinci Resolve) — research

Planned video noise reduction that preserves detail, focused first on **high-quality camera-log
footage with visible sensor noise**. The goal is natural skin, fine texture and stable motion,
with a fast **Preview** mode for editing and a full **Final** mode for rendering.

> **Status: research.** Noise model and estimators, a spatial filter and a motion-compensated
> temporal filter exist and are measured against ground truth (docs/RESEARCH.md). A slow CPU
> research prototype effect is built in CI for testing only (not released, untested in hosts).
> Premiere on Windows delivers neighbour frames on the CUDA GPU path (host test 2026-11-13).
> Free and without warranty — see the [root README](../../README.md#️-no-guarantee).

Current work: multi-frame dependency protocol (probe build 3), GPU port of the selected filters,
validation on real log footage.

- [Research and validation plan](docs/RESEARCH_PLAN.md) — experiments, comparisons and gates
- [Sequence benchmark](docs/SEQUENCE_BENCHMARK.md) — executable controls, float export and scoring
- [Host probes](../../shared/engine/probes/README.md) — builds, verification boundary and host matrix
- [Research findings](docs/RESEARCH.md) — measured results and unresolved limitations
- [Primary sources](docs/SOURCES.md) — papers, SDK references and reference-use constraints
- [Specification](AGENTS.md) · [Progress](PROGRESS.md)

## Run the current research tests

```bash
cmake -S plugins/denoise -B build-denoise -DCMAKE_BUILD_TYPE=Release
cmake --build build-denoise --config Release --parallel
ctest --test-dir build-denoise -C Release --output-on-failure
```

Run from the repository root. Python 3 enables the end-to-end CLI test. The build also produces
`dn_eval_tool` with `profile`, `temporal` and `baseline` reports and `dn_sequence_tool` with
`benchmark`, `export` and `score` modes (under `Release/` on multi-configuration builds).
These evaluate the measurement foundation; they do not render a denoised video.
