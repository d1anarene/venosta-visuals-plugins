// Synthetic test music with known hit times (deterministic: fixed-seed noise).
import type { Audio } from "../src/core/audio";

export function rng(seed: number) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

export interface Track {
  audio: Audio;
  kicks: number[];
  hats: number[];
}

/** Kick: pitch drop 110 -> 48 Hz, 1 ms attack, ~120 ms decay. Hats: 3-14 kHz noise, 40 ms. Bass: 70 Hz sustained. */
export function makeTrack(opts: { bpm: number; seconds: number; rate?: number; start?: number; kickLevels?: (k: number) => number;
  hats?: boolean; bass?: boolean; noise?: number; swingMs?: (k: number) => number }): Track {
  const rate = opts.rate ?? 48000;
  const n = Math.round(opts.seconds * rate);
  const l = new Float32Array(n);
  const r = rng(7);
  const beat = 60 / opts.bpm;
  const kicks: number[] = [], hats: number[] = [];
  for (let k = 0; ; ++k) {
    const t0 = (opts.start ?? 0.25) + k * beat + (opts.swingMs ? opts.swingMs(k) / 1000 : 0);
    if (t0 > opts.seconds - 0.3) break;
    kicks.push(t0);
    const amp = opts.kickLevels ? opts.kickLevels(k) : 0.8;
    let phase = 0;
    for (let i = Math.round(t0 * rate), j = 0; i < n && j < 0.4 * rate; ++i, ++j) {
      const t = j / rate;
      const f = 48 + 62 * Math.exp(-t / 0.03);
      phase += (2 * Math.PI * f) / rate;
      const env = Math.min(1, t / 0.001) * Math.exp(-t / 0.12);
      l[i] += amp * env * Math.sin(phase);
    }
    if (opts.hats ?? true) {
      const th = t0 + beat / 2;
      if (th < opts.seconds - 0.1) {
        hats.push(th);
        let prev = 0;
        for (let i = Math.round(th * rate), j = 0; i < n && j < 0.04 * rate; ++i, ++j) {
          const w = r() * 2 - 1;
          const hp = w - prev;  // crude high-pass
          prev = w;
          l[i] += 0.25 * hp * Math.exp(-(j / rate) / 0.012);
        }
      }
    }
  }
  if (opts.bass ?? true) for (let i = 0; i < n; ++i) l[i] += 0.08 * Math.sin((2 * Math.PI * 70 * i) / rate) * (0.6 + 0.4 * Math.sin((2 * Math.PI * 0.3 * i) / rate));
  const nz = opts.noise ?? 0.003;
  for (let i = 0; i < n; ++i) l[i] += nz * (r() * 2 - 1);
  return { audio: { sampleRate: rate, channels: [l, l.slice()] }, kicks, hats };
}

export function writeWav16(a: Audio): ArrayBuffer {
  const ch = a.channels.length, frames = a.channels[0].length, bytes = frames * ch * 2;
  const buf = new ArrayBuffer(44 + bytes);
  const v = new DataView(buf);
  const w = (o: number, s: string) => { for (let i = 0; i < 4; ++i) v.setUint8(o + i, s.charCodeAt(i)); };
  w(0, "RIFF"); v.setUint32(4, 36 + bytes, true); w(8, "WAVE"); w(12, "fmt "); v.setUint32(16, 16, true);
  v.setUint16(20, 1, true); v.setUint16(22, ch, true); v.setUint32(24, a.sampleRate, true);
  v.setUint32(28, a.sampleRate * ch * 2, true); v.setUint16(32, ch * 2, true); v.setUint16(34, 16, true);
  w(36, "data"); v.setUint32(40, bytes, true);
  let o = 44;
  for (let f = 0; f < frames; ++f) for (let c = 0; c < ch; ++c, o += 2) v.setInt16(o, Math.max(-32768, Math.min(32767, Math.round(a.channels[c][f] * 32767))), true);
  return buf;
}

export function writeAiff16(a: Audio): ArrayBuffer {
  const ch = a.channels.length, frames = a.channels[0].length, bytes = frames * ch * 2;
  const buf = new ArrayBuffer(12 + 26 + 16 + bytes);
  const v = new DataView(buf);
  const w = (o: number, s: string) => { for (let i = 0; i < 4; ++i) v.setUint8(o + i, s.charCodeAt(i)); };
  w(0, "FORM"); v.setUint32(4, buf.byteLength - 8); w(8, "AIFF");
  w(12, "COMM"); v.setUint32(16, 18); v.setUint16(20, ch); v.setUint32(22, frames); v.setUint16(26, 16);
  // 80-bit extended sample rate
  const rate = a.sampleRate;
  const e = Math.floor(Math.log2(rate));
  v.setUint16(28, 16383 + e);
  const mant = rate / 2 ** e;  // 1 <= mant < 2
  v.setUint32(30, Math.floor(mant * 2 ** 31) >>> 0);
  v.setUint32(34, 0);
  w(38, "SSND"); v.setUint32(42, 8 + bytes); v.setUint32(46, 0); v.setUint32(50, 0);
  let o = 54;
  for (let f = 0; f < frames; ++f) for (let c = 0; c < ch; ++c, o += 2) v.setInt16(o, Math.round(a.channels[c][f] * 32767));
  return buf;
}
