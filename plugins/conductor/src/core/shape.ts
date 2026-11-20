/**
 * Turning a signal into parameter values over time. Two modes (AGENTS.md §3):
 *  - continuous: level (dB) through an input window and a transfer curve onto an output range;
 *  - triggered: each hit becomes an event envelope (build-up, hold, release, peak offset, shaped
 *    rise and fall, amplitude fixed or by hit strength), combined by max / add / restart.
 * Both return a function of time (seconds, timeline) -> value. Pure, deterministic.
 */
import { applyCurve, Curve, LINEAR } from "./curves";
import { Envelope, envAt, smooth } from "./envelope";
import { Hit } from "./onsets";

export interface ContinuousOptions {
  inMinDb: number;
  inMaxDb: number;
  outMin: number;
  outMax: number;
  curve: Curve;
  invert: boolean;
  /** Smoothing in seconds (0 = none). */
  attack: number;
  release: number;
  /** Below this level (dB) the output is outMin (or outMax when inverted); -Infinity = off. */
  gateDb: number;
}

export const DEFAULT_CONTINUOUS: ContinuousOptions = {
  inMinDb: -40, inMaxDb: -6, outMin: 0, outMax: 1, curve: LINEAR, invert: false, attack: 0.005, release: 0.12, gateDb: -Infinity,
};

export function continuous(e: Envelope, options: Partial<ContinuousOptions> = {}): (t: number) => number {
  const o = { ...DEFAULT_CONTINUOUS, ...options };
  const env = o.attack > 0 || o.release > 0 ? smooth(e, o.attack, o.release) : e;
  const span = o.inMaxDb - o.inMinDb || 1;
  return (t: number) => {
    const db = envAt(env, t);
    let u = db < o.gateDb ? 0 : (db - o.inMinDb) / span;
    u = applyCurve(o.curve, u);
    if (o.invert) u = 1 - u;
    return o.outMin + (o.outMax - o.outMin) * u;
  };
}

export interface TriggerOptions {
  /** Value without events. */
  base: number;
  /** Value at an event's peak (at full amplitude). */
  peak: number;
  /** Build-up time before the peak (s). */
  rise: number;
  /** Time at full value after the peak (s). */
  hold: number;
  /** Time back to base after the hold (s). */
  release: number;
  /** Shift of the event peak relative to the hit's peak (s); negative = earlier. */
  peakOffset: number;
  /** Align to the hit's "peak" (default) or its "onset". */
  align: "peak" | "onset";
  riseCurve: Curve;
  /** Applied to the release progress: value falls as 1 - fallCurve(p). */
  fallCurve: Curve;
  /** Amplitude 1 for every hit, or by hit level between minDb (-> minAmp) and maxDb (-> 1). */
  amplitude: { mode: "fixed" } | { mode: "velocity"; minDb: number; maxDb: number; minAmp: number; curve: Curve };
  overlap: "max" | "add" | "restart";
}

export const DEFAULT_TRIGGER: TriggerOptions = {
  base: 0, peak: 1, rise: 0.05, hold: 0, release: 0.25, peakOffset: 0, align: "peak",
  riseCurve: { kind: "easeIn" }, fallCurve: { kind: "easeOut" }, amplitude: { mode: "fixed" }, overlap: "max",
};

export interface Event {
  /** Peak time (s, timeline). */
  at: number;
  amp: number;
}

export function eventsFromHits(hits: Hit[], options: Partial<TriggerOptions> = {}): Event[] {
  const o = { ...DEFAULT_TRIGGER, ...options };
  return hits.map((h) => {
    let amp = 1;
    if (o.amplitude.mode === "velocity") {
      const a = o.amplitude;
      const u = applyCurve(a.curve, (h.peakDb - a.minDb) / (a.maxDb - a.minDb || 1));
      amp = a.minAmp + (1 - a.minAmp) * u;
    }
    return { at: (o.align === "onset" ? h.onsetTime : h.peakTime) + o.peakOffset, amp };
  });
}

/** Envelope of one event at time t relative to its peak (0..1). */
function eventShape(o: TriggerOptions, dt: number): number {
  if (dt < -o.rise) return 0;
  if (dt < 0) return o.rise > 0 ? applyCurve(o.riseCurve, (dt + o.rise) / o.rise) : 1;
  if (dt <= o.hold) return 1;
  const p = o.release > 0 ? (dt - o.hold) / o.release : 1;
  return p >= 1 ? 0 : 1 - applyCurve(o.fallCurve, p);
}

export function triggered(events: Event[], options: Partial<TriggerOptions> = {}): (t: number) => number {
  const o = { ...DEFAULT_TRIGGER, ...options };
  const ev = [...events].sort((a, b) => a.at - b.at);
  const before = o.rise, after = o.hold + o.release;
  return (t: number) => {
    // events whose span [at - rise, at + hold + release] contains t
    let lo = 0, hi = ev.length;
    while (lo < hi) {  // first event with at >= t - after
      const m = (lo + hi) >> 1;
      if (ev[m].at < t - after) lo = m + 1;
      else hi = m;
    }
    let acc = 0;
    for (let i = lo; i < ev.length && ev[i].at - before <= t; ++i) {
      const v = eventShape(o, t - ev[i].at) * ev[i].amp;
      if (o.overlap === "add") acc += v;
      else if (o.overlap === "max") acc = Math.max(acc, v);
      else acc = v;  // restart: the latest started event wins
    }
    return o.base + (o.peak - o.base) * acc;
  };
}
