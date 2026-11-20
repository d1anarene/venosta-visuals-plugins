import { describe, expect, it } from "vitest";
import { maxError, TICKS_PER_SECOND } from "../src/core/keyframes";
import { analyse, DEFAULT_SETTINGS, plan, Settings } from "../src/panel/plan";
import { strings } from "../src/panel/i18n";
import { makeTrack } from "./synth";

const T = TICKS_PER_SECOND;
const tpf = 10594584000;  // 23.976 fps

describe("plan (music -> keyframes on a clip)", () => {
  const { audio, kicks } = makeTrack({ bpm: 120, seconds: 12 });
  // audio clip: starts at 2 s on the timeline, trimmed by 0.5 s at the head
  // target clip: 1 s .. 9 s on the timeline, its media starts 3 s into the source file
  const timing = { audioStart: 2 * T, audioIn: 0.5 * T, audioOut: 11.5 * T, targetStart: 1 * T, targetEnd: 9 * T, targetIn: 3 * T, ticksPerFrame: tpf };
  const s: Settings = { ...DEFAULT_SETTINGS, density: "frame" };
  const an = analyse(audio, s, 0.5, 11.5);
  const p = plan(an, s, timing);
  const timelineKicks = kicks.map((k) => 2 + (k - 0.5)).filter((t) => t >= 1 && t < 9 && t >= 2);

  it("finds the hits inside the target range, in timeline time", () => {
    expect(p.hitTimes.length).toBe(timelineKicks.length);
    for (const t of timelineKicks) expect(p.hitTimes.some((h) => Math.abs(h - t) < 0.04)).toBe(true);
  });

  it("puts one key per sequence frame across the target clip, in the clip's media time", () => {
    const frames = Math.ceil((9 * T - 1 * T) / tpf);
    expect(p.full.length).toBeGreaterThanOrEqual(frames - 1);
    expect(p.full.length).toBeLessThanOrEqual(frames + 1);
    const first = Math.ceil(T / tpf) * tpf;
    expect(p.full[0].ticks).toBe(3 * T + (first - T));
  });

  it("peaks at every kick (within one frame) and rests at the base value between them", () => {
    const keyToTimeline = (ticks: number) => (ticks - 3 * T + 1 * T) / T;
    for (const t of timelineKicks) {
      const near = p.full.filter((k) => Math.abs(keyToTimeline(k.ticks) - t) <= 1.5 * (tpf / T));
      const top = Math.max(...near.map((k) => k.value));
      expect(top).toBeCloseTo(120, 6);  // peaks snapped onto frames: the full value is written
    }
    const quiet = p.full.filter((k) => timelineKicks.every((t) => Math.abs(keyToTimeline(k.ticks) - t) > 0.35));
    expect(quiet.length).toBeGreaterThan(10);
    for (const k of quiet) expect(k.value).toBeCloseTo(100, 6);
  });

  it("optimised keys stay within the tolerance and are far fewer", () => {
    const o = plan(an, { ...s, density: "optimised", tolerance: 0.01 }, timing);
    expect(o.keys.length).toBeLessThan(p.full.length * 0.75);
    expect(maxError(p.full, o.keys)).toBeLessThanOrEqual(0.2 + 1e-9);  // 1 % of 100..120
  });

  it("without snapping, frame sampling can miss a short peak (why snapping is on)", () => {
    const raw = plan(an, { ...s, snapPeaks: false }, timing);
    const tops = timelineKicks.map((t) => Math.max(...raw.full.filter((k) => Math.abs((k.ticks - 2 * T) / T - t) <= 0.06).map((k) => k.value)));
    expect(Math.min(...tops)).toBeLessThan(119.9);
  });

  it("sequence time base writes timeline ticks", () => {
    const q = plan(an, { ...s, keyTimeBase: "sequence" }, timing);
    expect(q.full[0].ticks).toBe(Math.ceil(T / tpf) * tpf);
  });

  it("continuous mode stays inside the output range", () => {
    const c = plan(an, { ...s, mode: "continuous", outMin: 0, outMax: 50 }, timing);
    for (const k of c.full) {
      expect(k.value).toBeGreaterThanOrEqual(0);
      expect(k.value).toBeLessThanOrEqual(50);
    }
  });
});

describe("i18n", () => {
  it("German for de_DE, English otherwise, same keys", () => {
    expect(strings("de_DE").write).toBe("Keyframes schreiben");
    expect(strings("fr_FR").write).toBe("Write keyframes");
    expect(Object.keys(strings("de_DE")).sort()).toEqual(Object.keys(strings("en_US")).sort());
  });
});
