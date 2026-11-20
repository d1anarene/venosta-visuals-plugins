/**
 * Level envelope at a fixed control rate (default 1000 Hz = 1 ms steps): centred RMS (no lag)
 * in dBFS. Optional attack/release smoothing is an artistic control applied later (shape.ts).
 */
export interface Envelope {
  /** Control rate in Hz (values per second). */
  rate: number;
  /** dBFS per step; step i is centred on time i / rate. -Infinity = silence. */
  db: Float32Array;
}

export const SILENCE_DB = -120;

export function rmsEnvelope(signal: Float32Array, sampleRate: number, windowSec = 0.01, rate = 1000): Envelope {
  // Direct sum per step (no prefix array): memory stays at the size of the result, which matters
  // for long songs inside Premiere's panel runtime.
  const steps = Math.max(1, Math.ceil((signal.length / sampleRate) * rate));
  const half = Math.max(1, Math.round((windowSec * sampleRate) / 2));
  const db = new Float32Array(steps);
  for (let k = 0; k < steps; ++k) {
    const c = Math.round((k / rate) * sampleRate);
    const a = Math.max(0, c - half), b = Math.min(signal.length, c + half);
    let sum = 0;
    for (let i = a; i < b; ++i) sum += signal[i] * signal[i];
    const ms = b > a ? sum / (b - a) : 0;
    db[k] = ms > 1e-12 ? 10 * Math.log10(ms) : SILENCE_DB;
  }
  return { rate, db };
}

/** Value at time t (linear interpolation between steps, clamped). */
export function envAt(e: Envelope, t: number): number {
  const x = t * e.rate;
  if (x <= 0) return e.db[0];
  const i = Math.floor(x);
  if (i >= e.db.length - 1) return e.db[e.db.length - 1];
  const f = x - i;
  return e.db[i] + (e.db[i + 1] - e.db[i]) * f;
}

/** One-pole attack/release smoothing in dB (attack: rising, release: falling), seconds. */
export function smooth(e: Envelope, attack: number, release: number): Envelope {
  const out = new Float32Array(e.db.length);
  const ca = attack > 0 ? Math.exp(-1 / (attack * e.rate)) : 0;
  const cr = release > 0 ? Math.exp(-1 / (release * e.rate)) : 0;
  let y = e.db[0] ?? SILENCE_DB;
  for (let i = 0; i < e.db.length; ++i) {
    const x = e.db[i];
    const c = x > y ? ca : cr;
    y = c * y + (1 - c) * x;
    out[i] = y;
  }
  return { rate: e.rate, db: out };
}
