/**
 * Frequency bands. Butterworth-style band-pass from cascaded RBJ biquads (high-pass at `low`,
 * low-pass at `high`, 12 dB/oct per section), run forwards and backwards (zero phase): a hit is
 * not delayed by the filter, so keyframes land where the sound is. Pure, deterministic.
 */
export interface Band {
  /** Hz; 0 = no high-pass. */
  low: number;
  /** Hz; 0 or >= Nyquist = no low-pass. */
  high: number;
}

export const BAND_PRESETS: Record<string, Band> = {
  full: { low: 0, high: 0 },
  sub: { low: 20, high: 60 },
  kick: { low: 40, high: 120 },
  bass: { low: 60, high: 250 },
  snareBody: { low: 150, high: 400 },
  snareCrack: { low: 2000, high: 5000 },
  vocals: { low: 300, high: 3400 },
  hihats: { low: 6000, high: 14000 },
  air: { low: 10000, high: 20000 },
};

interface Biquad { b0: number; b1: number; b2: number; a1: number; a2: number }

function design(kind: "lp" | "hp", f: number, rate: number, q: number): Biquad {
  const w = (2 * Math.PI * f) / rate, cs = Math.cos(w), al = Math.sin(w) / (2 * q);
  const a0 = 1 + al;
  const b1 = kind === "lp" ? 1 - cs : -(1 + cs);
  const b0 = kind === "lp" ? (1 - cs) / 2 : (1 + cs) / 2;
  return { b0: b0 / a0, b1: b1 / a0, b2: b0 / a0, a1: (-2 * cs) / a0, a2: (1 - al) / a0 };
}

function run(x: Float32Array, s: Biquad, reverse: boolean): void {
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  const n = x.length;
  for (let k = 0; k < n; ++k) {
    const i = reverse ? n - 1 - k : k;
    const xi = x[i];
    const y = s.b0 * xi + s.b1 * x1 + s.b2 * x2 - s.a1 * y1 - s.a2 * y2;
    x2 = x1; x1 = xi; y2 = y1; y1 = y;
    x[i] = y;
  }
}

/** Q values of a 4th-order Butterworth as two biquads. */
const Q4 = [0.5411961, 1.3065630];

/** Zero-phase band-pass (each edge 4th order, applied forwards and backwards = 8th order magnitude). */
export function bandpass(signal: Float32Array, rate: number, band: Band): Float32Array {
  const out = signal.slice();
  const nyq = rate / 2;
  const sections: Biquad[] = [];
  if (band.low > 0 && band.low < nyq) for (const q of Q4) sections.push(design("hp", band.low, rate, q));
  if (band.high > 0 && band.high < nyq * 0.98) for (const q of Q4) sections.push(design("lp", band.high, rate, q));
  for (const s of sections) run(out, s, false);
  for (const s of sections) run(out, s, true);
  return out;
}
