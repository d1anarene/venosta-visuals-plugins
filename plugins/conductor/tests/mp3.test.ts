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
  it("decodes with the encoder delay removed (hits at their original times)", async () => {
    const a = await decodeMp3(buf);
    expect(a.sampleRate).toBe(48000);
    expect(a.channels.length).toBe(2);
    expect(a.channels[0].length / a.sampleRate).toBeCloseTo(6, 1);
    const l = a.channels[0];
    let first = -1;
    for (let i = Math.round(0.2 * a.sampleRate); i < l.length; ++i) if (Math.abs(l[i]) > 0.3) { first = i / a.sampleRate; break; }
    expect(Math.abs(first - 0.2508)).toBeLessThan(0.002);
  });
});
