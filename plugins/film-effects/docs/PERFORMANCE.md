# Performance

## Measured: CPU reference (this is NOT the GPU path)

Hardware: Linux container, Intel Xeon @ 2.1 GHz, **4 vCPU**. Release build, scene `night-street`.
Command: `./build/fe_render --scene night-street --size WxH --preset "<preset>" --bench 7`

| Preset | 1920×1080 median / p95 | 3840×2160 median / p95 |
| --- | --- | --- |
| Fine 35mm Grain (grain only) | 102 / 107 ms | 411 / 420 ms |
| Soft Highlight Glow (bloom only) | 146 / 174 ms | 618 / 639 ms |
| Subtle Film (all three) | 379 / 403 ms | 1421 / 1451 ms |
| Night Lights (all three, stronger) | 393–405 / 456–470 ms | 1420–1456 / 1574–1671 ms |

Extra peak memory of one `process()` call at 3840×2160, measured with `fe_render --mem` (on top
of the 126 MB float working copy of the frame):

| Preset | Before (2026-11-10) | Now |
| --- | --- | --- |
| Night Lights (all three) | 570 MB | 253 MB |
| Soft Highlight Glow (bloom only) | 487 MB | 228 MB |
| Fine 35mm Grain (grain only) | 126 MB | 0 MB |

How: no copy of the original frame (mix/alpha applied in one in-place final pass), no stored
linear-light copy (linearised per pixel), the finest bloom blur accumulates directly, coarse
pyramid levels are released as soon as they are used, halation stored as one channel. The output
is bit-identical to the previous version (hash of all 56 scene × preset float outputs).

These numbers show the CPU path is a **correct fallback, not real-time**. Real-time playback is the
goal of the Metal path.

## Not measured yet

- Metal GPU path (1080p25, 4K25, 4K30; 8-bit SDR and float) — needs a Mac.
- Premiere end-to-end playback / export times.

Planned protocol: Premiere sequence 4K25 with Film Effects on an adjustment layer, playback at Full
resolution with the GPU renderer, dropped-frame indicator on; export a 60 s ProRes 422 HQ and record
wall time. Log median and p95 per-frame render time from the plugin's debug timer.
