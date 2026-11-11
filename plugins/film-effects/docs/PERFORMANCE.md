# Performance

## Measured: CPU reference (this is NOT the GPU path)

Hardware: Linux container, Intel Xeon @ 2.1 GHz, **4 vCPU**. Release build, scene `night-street`.
Command: `./build/fe_render --scene night-street --size WxH --preset "<preset>" --bench 7`

| Preset | 1920×1080 median / p95 | 3840×2160 median / p95 |
| --- | --- | --- |
| Fine 35mm Grain (grain only) | 134 / 143 ms | 416 / 433 ms |
| Soft Highlight Glow (bloom only) | 218 / 222 ms | 877 / 939 ms |
| Subtle Film (all three) | 416 / 455 ms | 1759 / 1787 ms |
| Night Lights (all three, stronger) | 467 / 499 ms | 1888 / 1950 ms |

Peak memory (whole CLI process, 4K, Night Lights): ~775 MB. The CPU path keeps several full-size
float copies (input, linear RGB, bloom/halation planes); reducing this is a planned optimisation.

These numbers show the CPU path is a **correct fallback, not real-time**. Real-time playback is the
goal of the Metal path.

## Not measured yet

- Metal GPU path (1080p25, 4K25, 4K30; 8-bit SDR and float) — needs a Mac.
- Premiere end-to-end playback / export times.

Planned protocol: Premiere sequence 4K25 with Film Effects on an adjustment layer, playback at Full
resolution with the GPU renderer, dropped-frame indicator on; export a 60 s ProRes 422 HQ and record
wall time. Log median and p95 per-frame render time from the plugin's debug timer.
