/** Minimal RIFF/WAVE reader for the feasibility spike: PCM 16/24/32-bit and float 32/64.
 *  Pure functions (no UXP), unit-tested. */
export interface WavInfo {
  format: "pcm" | "float";
  channels: number;
  sampleRate: number;
  bitsPerSample: number;
  dataOffset: number;
  dataBytes: number;
  frames: number;
}

function tag(v: DataView, o: number): string {
  return String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
}

export function parseWav(buf: ArrayBuffer): WavInfo {
  const v = new DataView(buf);
  if (buf.byteLength < 12 || tag(v, 0) !== "RIFF" || tag(v, 8) !== "WAVE") throw new Error("not a RIFF/WAVE file");
  let o = 12;
  let fmt: Omit<WavInfo, "dataOffset" | "dataBytes" | "frames"> | null = null;
  while (o + 8 <= buf.byteLength) {
    const id = tag(v, o);
    const size = v.getUint32(o + 4, true);
    const body = o + 8;
    if (id === "fmt ") {
      let code = v.getUint16(body, true);
      const channels = v.getUint16(body + 2, true);
      const sampleRate = v.getUint32(body + 4, true);
      const bitsPerSample = v.getUint16(body + 14, true);
      if (code === 0xfffe && size >= 40) code = v.getUint16(body + 24, true);  // WAVE_FORMAT_EXTENSIBLE
      if (code !== 1 && code !== 3) throw new Error(`unsupported WAV format code ${code}`);
      fmt = { format: code === 3 ? "float" : "pcm", channels, sampleRate, bitsPerSample };
    } else if (id === "data") {
      if (!fmt) throw new Error("data chunk before fmt chunk");
      const dataBytes = Math.min(size, buf.byteLength - body);
      const frames = Math.floor(dataBytes / ((fmt.bitsPerSample / 8) * fmt.channels));
      return { ...fmt, dataOffset: body, dataBytes, frames };
    }
    o = body + size + (size & 1);
  }
  throw new Error("no data chunk");
}

/** Sample (frame f, channel c) as a float in -1..1. */
export function sampleAt(v: DataView, w: WavInfo, f: number, c: number): number {
  const bytes = w.bitsPerSample / 8;
  const o = w.dataOffset + (f * w.channels + c) * bytes;
  if (w.format === "float") return w.bitsPerSample === 64 ? v.getFloat64(o, true) : v.getFloat32(o, true);
  switch (w.bitsPerSample) {
    case 16: return v.getInt16(o, true) / 32768;
    case 24: {
      const x = v.getUint8(o) | (v.getUint8(o + 1) << 8) | (v.getInt8(o + 2) << 16);
      return x / 8388608;
    }
    case 32: return v.getInt32(o, true) / 2147483648;
    default: throw new Error(`unsupported PCM bit depth ${w.bitsPerSample}`);
  }
}

/** RMS level in dBFS of every window of `windowSec` (all channels mixed), from the start. */
export function rmsEnvelopeDb(buf: ArrayBuffer, w: WavInfo, windowSec: number): number[] {
  const v = new DataView(buf);
  const n = Math.max(1, Math.round(windowSec * w.sampleRate));
  const out: number[] = [];
  for (let start = 0; start < w.frames; start += n) {
    const end = Math.min(w.frames, start + n);
    let s = 0;
    for (let f = start; f < end; ++f) {
      let m = 0;
      for (let c = 0; c < w.channels; ++c) m += sampleAt(v, w, f, c);
      m /= w.channels;
      s += m * m;
    }
    const rms = Math.sqrt(s / (end - start));
    out.push(rms > 0 ? 20 * Math.log10(rms) : -Infinity);
  }
  return out;
}
