# Roadmap

Free plugins planned by the owner, built one at a time, each for **Premiere Pro and DaVinci
Resolve** (macOS Metal, Windows CUDA, CPU fallback) where it is an effect. Order may change.

| # | Plugin | Idea | Status |
| --- | --- | --- | --- |
| 1 | **Film Effects** + Film Gate | One panel: Bloom, Halation, Grain, and new **Film Gate** (gate weave, flicker, gate edges, dust & scratches, all procedural). Standalone Bloom/Halation/Grain effects retired. | released (v1.0.12), Film Gate awaiting host test |
| 2 | **Lens & Diffusion** | Diffusion filter (mist looks), vignette, chromatic aberration, anamorphic streaks. On the shared engine. | released with CPU + Metal + CUDA, awaiting host test |
| 3 | **Exposure & False Color** | False color (stops from middle grey), zebras, clipping, split view, legend — viewing aid. | released (CPU + Metal + CUDA), awaiting host test |
| 4 | **Retro** | Tape/VHS artefacts, CRT lines, analogue colour bleed. | planned |
| 5 | **Denoise** | High-quality temporal + spatial noise reduction that preserves detail. **Plan and research first, real maths, no shortcuts.** Fast preview mode for timeline work (switchable), full-quality mode for renders. | planned (research first) |
| 6 | **Retime** | Optical-flow frame interpolation / slow motion, better than Premiere's Optical Flow. Research-heavy. | planned (research first) |

Rules for every plugin: see [AGENTS.md](AGENTS.md) (honest verification, README updates, no Adobe
SDK files in this repository).
