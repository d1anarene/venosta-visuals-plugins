# Roadmap

Free plugins planned by the owner, built one at a time, each for **Premiere Pro and DaVinci
Resolve** (macOS Metal, Windows CUDA, CPU fallback) where it is an effect. Order may change.

| # | Plugin | Idea | Status |
| --- | --- | --- | --- |
| 1 | **Film Effects** + Film Gate | Free film finishing effects in one panel: Bloom, Halation, Grain, and optional **Film Gate** (gate weave, flicker, gate edges, dust & scratches, all procedural). Standalone Bloom/Halation/Grain effects retired. | released (v1.0.12), Film Gate awaiting host test |
| 2 | **Lens & Diffusion** | Diffusion filter (mist looks), vignette, chromatic aberration, anamorphic streaks. On the shared engine. | host-tested in Premiere Pro 27 and DaVinci Resolve on macOS (Apple Silicon); Windows/Arm and Linux host tests pending |
| 3 | **Exposure & False Color** | False color (stops from middle grey), zebras, clipping, split view, legend — viewing aid. | host-tested in Premiere Pro 27 and DaVinci Resolve on macOS (Apple Silicon); Windows/Arm and Linux host tests pending |
| 4 | **Retro** | Colour bleed (Y'UV signal model with ringing), tape (jitter, tracking, dropouts, head switching), noise, CRT (scanlines, mask, curvature). | host-tested in Premiere Pro 27 and DaVinci Resolve on macOS (Apple Silicon); Windows/Arm and Linux host tests pending |
| 5 | **Denoise** | Detail-preserving temporal + spatial noise reduction, focused first on high-quality camera-log footage. **Research and measure before choosing algorithms.** Preview for editing, Final for renders. | D0/D1: [sequence benchmark](plugins/denoise/docs/SEQUENCE_BENCHMARK.md) + locally compiled temporal host probes; real-host/log calibration pending; no denoiser yet |
| 6 | **Retime** | Optical-flow frame interpolation / slow motion, better than Premiere's Optical Flow. Research-heavy. | planned (research first) |

Rules for every plugin: see [AGENTS.md](AGENTS.md) (honest verification, README updates, no Adobe
SDK files in this repository).
