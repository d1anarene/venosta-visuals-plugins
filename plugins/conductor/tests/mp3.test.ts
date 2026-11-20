import { describe, expect, it } from "vitest";
import { readFileSync } from "fs";
import { looksLikeMp3 } from "../src/core/audio";
import { decodeMp3 } from "../src/core/mp3";

// tests/fixtures/kick120.mp3: the synthetic test track (120 BPM, first kick at 0.25 s), encoded
// with ffmpeg/libmp3lame 192 kbit/s (encoder delay in the LAME header).
describe("mp3", () => {
  const raw = readFileSync(new URL("./fixtures/kick120.mp3", import.meta.url));
  const buf = raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength);

  it("is recognised", () => expect(looksLikeMp3(buf)).toBe(true));

  it("decodes in small steps with the encoder delay removed (hits at their original times)", async () => {
    let calls = 0;
    const a = await decodeMp3(buf, { chunkBytes: 8192, yieldFn: async () => { ++calls; } });
    expect(calls).toBeGreaterThan(10);
    expect(a.sampleRate).toBe(48000);
    expect(a.channels.length).toBe(2);
    expect(a.channels[0].length / a.sampleRate).toBeCloseTo(6, 1);
    const l = a.channels[0];
    let first = -1;
    for (let i = Math.round(0.2 * a.sampleRate); i < l.length; ++i) if (Math.abs(l[i]) > 0.3) { first = i / a.sampleRate; break; }
    expect(Math.abs(first - 0.2508)).toBeLessThan(0.002);
  });

  it("chunked decoding gives the same samples as one piece", async () => {
    const a = await decodeMp3(buf, { chunkBytes: 1 << 30 });
    const b = await decodeMp3(buf, { chunkBytes: 4096 });
    expect(b.channels[0].length).toBe(a.channels[0].length);
    let d = 0;
    for (let i = 0; i < a.channels[0].length; i += 7) d = Math.max(d, Math.abs(a.channels[0][i] - b.channels[0][i]));
    expect(d).toBe(0);
  });

  it("can be cancelled", async () => {
    await expect(decodeMp3(buf, { chunkBytes: 4096, isCancelled: () => true })).rejects.toThrow("cancelled");
  });
});
