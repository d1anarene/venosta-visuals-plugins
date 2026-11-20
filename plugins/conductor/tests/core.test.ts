import { describe, expect, it } from "vitest";
import {
  applyCurve, bandpass, BAND_PRESETS, continuous, decodeAudio, detectHits, estimateTempo, eventsFromHits, maxError,
  rmsEnvelope, sampleFrames, selectChannel, simplify, speedMap, TICKS_PER_SECOND, triggered,
} from "../src";
import { makeTrack, writeAiff16, writeWav16 } from "./synth";

const near = (got: number[], want: number[], tol: number) => {
  const misses = want.filter((w) => !got.some((g) => Math.abs(g - w) <= tol));
  const extras = got.filter((g) => !want.some((w) => Math.abs(g - w) <= tol));
  return { misses, extras };
};

describe("decode", () => {
  const { audio } = makeTrack({ bpm: 120, seconds: 1 });
  for (const [name, enc] of [["WAV", writeWav16], ["AIFF", writeAiff16]] as const) {
    it(`round-trips 16-bit stereo ${name}`, () => {
      const back = decodeAudio(enc(audio));
      expect(back.sampleRate).toBe(48000);
      expect(back.channels.length).toBe(2);
      let err = 0;
      for (let i = 0; i < audio.channels[0].length; i += 97) err = Math.max(err, Math.abs(back.channels[0][i] - audio.channels[0][i]));
      expect(err).toBeLessThan(1e-4);
    });
  }
  it("selects channels", () => {
    const a = { sampleRate: 10, channels: [Float32Array.of(1, 1), Float32Array.of(0, -1)] };
    expect(Array.from(selectChannel(a, "mix"))).toEqual([0.5, 0]);
    expect(Array.from(selectChannel(a, "side"))).toEqual([0.5, 1]);
    expect(Array.from(selectChannel(a, "right"))).toEqual([0, -1]);
  });
});

describe("band filter", () => {
  it("passes the band and attenuates outside it", () => {
    const rate = 48000, n = rate;
    const tone = (f: number) => Float32Array.from({ length: n }, (_, i) => Math.sin((2 * Math.PI * f * i) / rate));
    const rms = (x: Float32Array) => Math.sqrt(x.slice(n / 4, (3 * n) / 4).reduce((s, v) => s + v * v, 0) / (n / 2));
    const k = BAND_PRESETS.kick;
    expect(rms(bandpass(tone(70), rate, k))).toBeGreaterThan(0.6);
    expect(rms(bandpass(tone(1000), rate, k))).toBeLessThan(0.001);
    expect(rms(bandpass(tone(10), rate, k))).toBeLessThan(0.01);
  });
  it("does not delay a hit (zero phase)", () => {
    const rate = 48000;
    const { audio, kicks } = makeTrack({ bpm: 120, seconds: 3, hats: false, bass: false });
    const env = rmsEnvelope(bandpass(selectChannel(audio, "mix"), rate, BAND_PRESETS.kick), rate);
    const raw = rmsEnvelope(selectChannel(audio, "mix"), rate);
    const peak = (e: typeof env, t: number) => {
      let p = Math.round(t * e.rate);
      for (let j = p; j < p + 80; ++j) if (e.db[j] > e.db[p]) p = j;
      return p / e.rate;
    };
    // zero phase: no systematic delay (a causal filter of this order would lag by ~10-20 ms);
    // individual peaks may move a little because the band changes the envelope's shape
    const shifts = kicks.map((t) => peak(env, t) - peak(raw, t));
    const mean = shifts.reduce((a, b) => a + b, 0) / shifts.length;
    expect(Math.abs(mean)).toBeLessThan(0.015);
    for (const d of shifts) expect(Math.abs(d)).toBeLessThan(0.02);
  });
});

describe("hit detection", () => {
  const rate = 48000;
  it("finds every kick in the kick band and ignores hats and bass", () => {
    const { audio, kicks } = makeTrack({ bpm: 124, seconds: 12 });
    const env = rmsEnvelope(bandpass(selectChannel(audio, "mix"), rate, BAND_PRESETS.kick), rate);
    const hits = detectHits(env);
    const r = near(hits.map((h) => h.onsetTime), kicks, 0.01);
    expect(r.misses).toEqual([]);
    expect(r.extras).toEqual([]);
    for (const h of hits) expect(h.peakTime - h.onsetTime).toBeGreaterThanOrEqual(0);
    for (const h of hits) expect(h.peakTime - h.onsetTime).toBeLessThan(0.04);
  });
  it("finds the hats in the hi-hat band, not the kicks", () => {
    const { audio, hats } = makeTrack({ bpm: 124, seconds: 8 });
    const env = rmsEnvelope(bandpass(selectChannel(audio, "mix"), rate, BAND_PRESETS.hihats), rate);
    const r = near(detectHits(env).map((h) => h.onsetTime), hats, 0.01);
    expect(r.misses).toEqual([]);
    expect(r.extras).toEqual([]);
  });
  it("follows a drifting tempo exactly (no fixed grid)", () => {
    const { audio, kicks } = makeTrack({ bpm: 120, seconds: 10, swingMs: (k) => 3 * k });  // 3 ms later every beat
    const env = rmsEnvelope(bandpass(selectChannel(audio, "mix"), rate, BAND_PRESETS.kick), rate);
    const r = near(detectHits(env).map((h) => h.onsetTime), kicks, 0.01);
    expect(r.misses).toEqual([]);
    expect(r.extras).toEqual([]);
  });
  it("reports hit strength (velocity)", () => {
    const { audio } = makeTrack({ bpm: 120, seconds: 6, kickLevels: (k) => (k % 2 ? 0.25 : 0.8), hats: false });
    const env = rmsEnvelope(bandpass(selectChannel(audio, "mix"), rate, BAND_PRESETS.kick), rate);
    const hits = detectHits(env);
    expect(hits.length).toBeGreaterThan(8);
    const loud = hits.filter((_, i) => i % 2 === 0).map((h) => h.peakDb);
    const soft = hits.filter((_, i) => i % 2 === 1).map((h) => h.peakDb);
    // 0.8 vs 0.25 = 10 dB apart; the bass in the same band lifts the soft ones a little
    expect(Math.min(...loud) - Math.max(...soft)).toBeGreaterThan(6);
  });
});

describe("tempo", () => {
  for (const bpm of [96, 120, 128, 140]) {
    it(`estimates ${bpm} BPM and the beat phase`, () => {
      const { audio, kicks } = makeTrack({ bpm, seconds: 16, start: 0.37 });
      const env = rmsEnvelope(selectChannel(audio, "mix"), 48000);
      const g = estimateTempo(env)!;
      expect(Math.abs(g.bpm - bpm)).toBeLessThan(0.3);
      const period = 60 / g.bpm;
      const d = ((kicks[0] - g.phase) % period + period) % period;
      expect(Math.min(d, period - d)).toBeLessThan(0.02);
    });
  }
});

describe("curves and shaping", () => {
  it("curves are monotonic with fixed ends", () => {
    for (const c of [{ kind: "linear" }, { kind: "easeIn" }, { kind: "easeOut" }, { kind: "easeInOut" },
      { kind: "power", exponent: 3 }, { kind: "exp", k: 4 }, { kind: "exp", k: -4 },
      { kind: "bezier", x1: 0.42, y1: 0, x2: 0.58, y2: 1 }] as const) {
      expect(applyCurve(c, 0)).toBeCloseTo(0, 6);
      expect(applyCurve(c, 1)).toBeCloseTo(1, 6);
      let prev = -1;
      for (let u = 0; u <= 1.0001; u += 0.01) {
        const v = applyCurve(c, u);
        expect(v).toBeGreaterThanOrEqual(prev - 1e-9);
        prev = v;
      }
    }
  });
  it("continuous: input window and output range", () => {
    const env = { rate: 1000, db: Float32Array.from({ length: 1000 }, (_, i) => -40 + (34 * i) / 999) };
    const f = continuous(env, { inMinDb: -40, inMaxDb: -6, outMin: 100, outMax: 130, attack: 0, release: 0 });
    expect(f(0)).toBeCloseTo(100, 3);
    expect(f(0.999)).toBeCloseTo(130, 1);
    expect(f(0.5)).toBeCloseTo(115, 0);
  });
  it("triggered: the event peaks exactly at the hit peak, with the chosen shape", () => {
    const hits = [{ onsetTime: 1, peakTime: 1.01, peakDb: -6, rise: 20 }];
    const o = { base: 100, peak: 500, rise: 0.2, hold: 0, release: 0.4, riseCurve: { kind: "linear" as const }, fallCurve: { kind: "linear" as const } };
    const f = triggered(eventsFromHits(hits, o), o);
    expect(f(1.01)).toBeCloseTo(500, 6);
    expect(f(0.81)).toBeCloseTo(100, 6);
    expect(f(0.91)).toBeCloseTo(300, 6);
    expect(f(1.21)).toBeCloseTo(300, 6);
    expect(f(1.5)).toBeCloseTo(100, 6);
    const early = triggered(eventsFromHits(hits, { ...o, peakOffset: -0.05 }), o);
    expect(early(0.96)).toBeCloseTo(500, 6);
  });
  it("triggered overlap policies", () => {
    const ev = [{ at: 1, amp: 1 }, { at: 1.1, amp: 0.5 }];
    const o = { base: 0, peak: 1, rise: 0, hold: 0, release: 0.5, fallCurve: { kind: "linear" as const } };
    expect(triggered(ev, { ...o, overlap: "max" })(1.1)).toBeCloseTo(0.8, 6);
    expect(triggered(ev, { ...o, overlap: "add" })(1.1)).toBeCloseTo(1.3, 6);
    expect(triggered(ev, { ...o, overlap: "restart" })(1.1)).toBeCloseTo(0.5, 6);
  });
});

describe("keyframes", () => {
  it("samples exact frame ticks and simplifies within tolerance", () => {
    const tpf = 10594584000;  // 23.976 fps
    const f = triggered([{ at: 1, amp: 1 }, { at: 2, amp: 1 }], { base: 100, peak: 120, rise: 0.05, release: 0.3 });
    const keys = sampleFrames(f, 0, 3 * TICKS_PER_SECOND, tpf);
    expect(keys.length).toBe(Math.ceil((3 * TICKS_PER_SECOND) / tpf));
    expect(keys[1].ticks).toBe(tpf);
    const reduced = simplify(keys, 0.25);
    expect(reduced.length).toBeLessThan(keys.length / 2);
    expect(maxError(keys, reduced)).toBeLessThanOrEqual(0.25);
    expect(reduced[0]).toEqual(keys[0]);
    expect(reduced[reduced.length - 1]).toEqual(keys[keys.length - 1]);
  });
});

describe("time map (speed ramps)", () => {
  const fps = 25, frame = 1 / fps;
  const hits = [1, 2, 3].map((t) => ({ at: t, amp: 1 }));
  const ramp = triggered(hits, { base: 1, peak: 5, rise: 0.2, release: 0.3, riseCurve: { kind: "linear" }, fallCurve: { kind: "linear" } });
  it("sequential: every ramp adds drift, peaks stay on the hits", () => {
    const m = speedMap(ramp, 0, 4 * fps, frame, "sequential");
    expect(m.drift).toBeGreaterThan(2.5);  // 3 ramps of about (5 - 1) * 0.25 s each
    const at = (t: number) => m.speed[Math.round(t / frame)];
    for (const h of [1, 2, 3]) expect(at(h)).toBeCloseTo(5, 6);
  });
  it("compensated: source and timeline meet at every hit", () => {
    const mild = triggered(hits, { base: 1, peak: 2, rise: 0.1, release: 0.15, riseCurve: { kind: "linear" }, fallCurve: { kind: "linear" } });
    const m = speedMap(mild, 0, 4 * fps, frame, "compensated", [1, 2, 3]);
    for (const h of [1, 2, 3]) expect(Math.abs(m.source[Math.round(h / frame)] - h)).toBeLessThan(1e-9);
    expect(Math.abs(m.drift)).toBeLessThan(1e-9);
    expect(m.minSpeed).toBeGreaterThan(0.5);
  });
  it("compensated: reports when a ramp cannot be balanced without stopping or reversing", () => {
    const m = speedMap(ramp, 0, 4 * fps, frame, "compensated", [1, 2, 3]);
    expect(m.minSpeed).toBeLessThanOrEqual(0);  // 500 % on every beat at 60 BPM: the panel must warn
  });
});
