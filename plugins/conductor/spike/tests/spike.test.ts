import { describe, expect, it } from "vitest";
import { parseWav, rmsEnvelopeDb, sampleAt } from "../src/wav";
import { pulseValues } from "../src/pulse";

function makeWav(opts: { bits: 16 | 24 | 32; float?: boolean; channels: number; rate: number; samples: number[][] }): ArrayBuffer {
  const { bits, channels, rate, samples } = opts;
  const frames = samples[0].length, bytes = bits / 8, dataBytes = frames * channels * bytes;
  const buf = new ArrayBuffer(44 + dataBytes);
  const v = new DataView(buf);
  const w = (o: number, s: string) => { for (let i = 0; i < 4; ++i) v.setUint8(o + i, s.charCodeAt(i)); };
  w(0, "RIFF"); v.setUint32(4, 36 + dataBytes, true); w(8, "WAVE");
  w(12, "fmt "); v.setUint32(16, 16, true); v.setUint16(20, opts.float ? 3 : 1, true); v.setUint16(22, channels, true);
  v.setUint32(24, rate, true); v.setUint32(28, rate * channels * bytes, true); v.setUint16(32, channels * bytes, true);
  v.setUint16(34, bits, true); w(36, "data"); v.setUint32(40, dataBytes, true);
  let o = 44;
  for (let f = 0; f < frames; ++f)
    for (let c = 0; c < channels; ++c, o += bytes) {
      const x = samples[c][f];
      if (opts.float) v.setFloat32(o, x, true);
      else if (bits === 16) v.setInt16(o, Math.round(x * 32767), true);
      else if (bits === 24) { const q = Math.round(x * 8388607); v.setUint8(o, q & 255); v.setUint8(o + 1, (q >> 8) & 255); v.setInt8(o + 2, q >> 16); }
      else v.setInt32(o, Math.round(x * 2147483647), true);
    }
  return buf;
}

describe("wav", () => {
  for (const [bits, float] of [[16, false], [24, false], [32, false], [32, true]] as const) {
    it(`reads ${bits}-bit ${float ? "float" : "PCM"} stereo`, () => {
      const l = [0, 0.5, -0.5, 0.25], r = [0.1, -0.1, 0.2, -0.2];
      const buf = makeWav({ bits, float, channels: 2, rate: 48000, samples: [l, r] });
      const info = parseWav(buf);
      expect(info).toMatchObject({ channels: 2, sampleRate: 48000, bitsPerSample: bits, frames: 4 });
      const v = new DataView(buf);
      for (let f = 0; f < 4; ++f) {
        expect(sampleAt(v, info, f, 0)).toBeCloseTo(l[f], 3);
        expect(sampleAt(v, info, f, 1)).toBeCloseTo(r[f], 3);
      }
    });
  }
  it("measures RMS of a sine at -6 dBFS peak as about -9 dB", () => {
    const rate = 48000, n = rate;
    const s = Array.from({ length: n }, (_, i) => 0.5 * Math.sin((2 * Math.PI * 1000 * i) / rate));
    const buf = makeWav({ bits: 16, channels: 1, rate, samples: [s] });
    const env = rmsEnvelopeDb(buf, parseWav(buf), 0.1);
    expect(env.length).toBe(10);
    for (const d of env) expect(d).toBeCloseTo(-9.03, 1);
  });
  it("rejects non-WAV data", () => {
    expect(() => parseWav(new ArrayBuffer(16))).toThrow();
  });
});

describe("pulse", () => {
  it("peaks every period at base + amplitude and decays to base", () => {
    const v = pulseValues(25, 100, 20, 12);
    expect(v.length).toBe(25);
    expect(v[0]).toBe(120);
    expect(v[12]).toBe(120);
    expect(v[24]).toBe(120);
    expect(v[10]).toBe(100);
    expect(v[1]).toBeLessThan(120);
    expect(v[1]).toBeGreaterThan(100);
  });
});
