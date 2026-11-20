/**
 * Average magnitude spectrum (Welch: Hann windows of `size` samples, up to `maxFrames` spread
 * evenly over the signal), in dB per FFT bin. Used for the EQ-like band picker. Pure.
 */
export interface Spectrum {
  /** Hz per bin. */
  binHz: number;
  /** dB (relative to a full-scale sine ~ 0 dB) for bins 0..size/2. */
  db: Float32Array;
}

/** In-place iterative radix-2 FFT (re, im of length 2^k). */
export function fft(re: Float64Array, im: Float64Array): void {
  const n = re.length;
  for (let i = 1, j = 0; i < n; ++i) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; ++k) {
        const ar = re[i + k + len / 2], ai = im[i + k + len / 2];
        const tr = ar * cr - ai * ci, ti = ar * ci + ai * cr;
        re[i + k + len / 2] = re[i + k] - tr;
        im[i + k + len / 2] = im[i + k] - ti;
        re[i + k] += tr;
        im[i + k] += ti;
        const nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
}

export function averageSpectrum(signal: Float32Array, rate: number, size = 4096, maxFrames = 256): Spectrum {
  const half = size / 2;
  const power = new Float64Array(half + 1);
  const win = new Float64Array(size);
  let wsum = 0;
  for (let i = 0; i < size; ++i) {
    win[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / size);
    wsum += win[i];
  }
  const frames = Math.max(1, Math.min(maxFrames, Math.floor(signal.length / size)));
  const step = frames > 1 ? (signal.length - size) / (frames - 1) : 0;
  const re = new Float64Array(size), im = new Float64Array(size);
  for (let f = 0; f < frames; ++f) {
    const o = Math.round(f * step);
    for (let i = 0; i < size; ++i) {
      re[i] = (signal[o + i] ?? 0) * win[i];
      im[i] = 0;
    }
    fft(re, im);
    for (let k = 0; k <= half; ++k) power[k] += re[k] * re[k] + im[k] * im[k];
  }
  const db = new Float32Array(half + 1);
  // amplitude of a full-scale sine in one bin = wsum / 2
  const ref = (wsum / 2) ** 2;
  for (let k = 0; k <= half; ++k) {
    const p = power[k] / frames / ref;
    db[k] = p > 1e-14 ? 10 * Math.log10(p) : -140;
  }
  return { binHz: rate / size, db };
}

/** Spectrum level (dB) at frequency f, maximum over the bins it spans when bins are coarse. */
export function spectrumAt(s: Spectrum, f: number, fNext = f): number {
  const a = Math.max(1, Math.floor(f / s.binHz)), b = Math.min(s.db.length - 1, Math.max(a, Math.ceil(fNext / s.binHz)));
  let m = -140;
  for (let k = a; k <= b; ++k) m = Math.max(m, s.db[k]);
  return m;
}
