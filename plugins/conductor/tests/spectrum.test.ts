import { describe, expect, it } from "vitest";
import { averageSpectrum, spectrumAt } from "../src/core/spectrum";

describe("spectrum", () => {
  it("puts a sine at its frequency with about 0 dB for full scale", () => {
    const rate = 48000, n = rate * 2;
    const x = Float32Array.from({ length: n }, (_, i) => Math.sin((2 * Math.PI * 1000 * i) / rate));
    const s = averageSpectrum(x, rate);
    let peak = 0;
    for (let k = 1; k < s.db.length; ++k) if (s.db[k] > s.db[peak]) peak = k;
    expect(Math.abs(peak * s.binHz - 1000)).toBeLessThan(s.binHz);
    expect(s.db[peak]).toBeGreaterThan(-1.5);
    expect(s.db[peak]).toBeLessThan(0.5);
    expect(spectrumAt(s, 5000)).toBeLessThan(-60);
  });
  it("shows a 60 Hz kick-range tone louder there than at 5 kHz", () => {
    const rate = 44100, n = rate;
    const x = Float32Array.from({ length: n }, (_, i) => 0.5 * Math.sin((2 * Math.PI * 60 * i) / rate));
    const s = averageSpectrum(x, rate);
    expect(spectrumAt(s, 55, 65)).toBeGreaterThan(spectrumAt(s, 4900, 5100) + 60);
  });
});
