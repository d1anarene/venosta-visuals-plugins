/**
 * Tempo and beat grid from an onset function: autocorrelation over 60-200 BPM with a broad prior
 * around 120 BPM (resolves half/double tempo), then a fine comb search over tempo and phase.
 */
import { Envelope } from "./envelope";
import { onsetStrength } from "./onsets";

export interface BeatGrid {
  bpm: number;
  /** Seconds of the first beat at or after 0. */
  phase: number;
  /** 0..1, share of onset energy on the grid relative to the best and worst phase. */
  confidence: number;
}

export function beatTime(g: BeatGrid, k: number): number {
  return g.phase + (k * 60) / g.bpm;
}

/** Onset function at 100 Hz (max of each 10 ms). */
function decimate(o: Float32Array, rate: number, target = 100): { o: Float32Array; rate: number } {
  const f = Math.max(1, Math.round(rate / target));
  const out = new Float32Array(Math.ceil(o.length / f));
  for (let i = 0; i < o.length; ++i) out[(i / f) | 0] = Math.max(out[(i / f) | 0], o[i]);
  return { o: out, rate: rate / f };
}

function combScore(o: Float32Array, rate: number, bpm: number, phase: number): number {
  const period = (60 / bpm) * rate;
  let s = 0;
  for (let t = phase * rate; t < o.length - 1; t += period) {
    const i = Math.floor(t), f = t - i;
    s += o[i] * (1 - f) + o[i + 1] * f;
  }
  return s;
}

export function estimateTempo(e: Envelope, minBpm = 60, maxBpm = 200): BeatGrid | null {
  const { o, rate } = decimate(onsetStrength(e), e.rate);
  if (o.length < rate * 4) return null;  // at least 4 s
  let mean = 0;
  for (const v of o) mean += v;
  mean /= o.length;
  const x = Float32Array.from(o, (v) => v - mean);
  let best = 0, bestLag = 0;
  const lagMin = Math.floor((60 / maxBpm) * rate), lagMax = Math.ceil((60 / minBpm) * rate);
  for (let lag = lagMin; lag <= lagMax && lag < x.length; ++lag) {
    let s = 0;
    for (let i = lag; i < x.length; ++i) s += x[i] * x[i - lag];
    s /= x.length - lag;
    const bpm = (60 * rate) / lag;
    const prior = Math.exp(-0.5 * (Math.log2(bpm / 120) / 0.9) ** 2);
    if (s * prior > best) { best = s * prior; bestLag = lag; }
  }
  if (!bestLag) return null;
  const coarse = (60 * rate) / bestLag;
  // fine search: tempo +-3 % in 0.01 BPM, phase over one period in 1/4 steps of the control rate
  let g: BeatGrid = { bpm: coarse, phase: 0, confidence: 0 };
  let top = -Infinity;
  for (let bpm = coarse * 0.97; bpm <= coarse * 1.03; bpm += 0.01) {
    const period = 60 / bpm;
    for (let ph = 0; ph < period; ph += 0.25 / rate) {
      const s = combScore(o, rate, bpm, ph);
      if (s > top) { top = s; g = { bpm, phase: ph, confidence: 0 }; }
    }
  }
  // confidence: best phase vs the worst phase at that tempo
  let worst = Infinity;
  const period = 60 / g.bpm;
  for (let ph = 0; ph < period; ph += 1 / rate) worst = Math.min(worst, combScore(o, rate, g.bpm, ph));
  g.confidence = top > 0 ? Math.max(0, Math.min(1, (top - worst) / top)) : 0;
  g.bpm = Math.round(g.bpm * 100) / 100;
  return g;
}
