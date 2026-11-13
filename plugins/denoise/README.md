# Denoise (Premiere Pro + DaVinci Resolve) — research

Planned video noise reduction that preserves detail, focused first on **high-quality camera-log
footage with visible sensor noise**. The goal is natural skin, fine texture and stable motion,
with a fast **Preview** mode for editing and a full **Final** mode for rendering.

> **Status: executable research benchmark and host probes.** Noise simulation/profiling, moving
> test sequences and detail/motion scoring exist. Research probes compile locally; no actual host
> testing has been performed. There is no denoising algorithm or production Denoise build yet.
> Free and without warranty — see the
> [root README](../../README.md#️-no-guarantee).

Current work: validate the benchmark on real log footage and exercise the temporal probes in both
hosts, then compare filtering methods before selecting a design.

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
