/**
 * Decoded audio and channel selection. Pure, deterministic.
 * Decoders for compressed formats (MP3/AAC) come later (WebAssembly or Premiere's own export);
 * this module reads RIFF/WAVE (PCM 16/24/32, float 32/64) and AIFF/AIFC ("NONE"/"sowt"/"fl32").
 */
export interface Audio {
  sampleRate: number;
  /** One Float32Array per channel, samples in -1..1. */
  channels: Float32Array[];
}

export type ChannelMode = "mix" | "left" | "right" | "mid" | "side";

export function duration(a: Audio): number {
  return a.channels.length ? a.channels[0].length / a.sampleRate : 0;
}

/** One analysis channel: L+R (mix), L, R, mid ((L+R)/2) or side ((L-R)/2). Mono input: always the channel. */
export function selectChannel(a: Audio, mode: ChannelMode): Float32Array {
  const [l, r] = a.channels;
  if (!l) return new Float32Array(0);
  if (!r) return l;
  const out = new Float32Array(l.length);
  switch (mode) {
    case "left": return l.slice();
    case "right": return r.slice();
    case "side":
      for (let i = 0; i < l.length; ++i) out[i] = 0.5 * (l[i] - r[i]);
      return out;
    default: {  // mix and mid: average of all channels
      const n = a.channels.length;
      for (const ch of a.channels) for (let i = 0; i < out.length; ++i) out[i] += ch[i] / n;
      return out;
    }
  }
}

function tag(v: DataView, o: number): string {
  return String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
}

function readPcm(v: DataView, o: number, bits: number, float: boolean, little: boolean): number {
  if (float) return bits === 64 ? v.getFloat64(o, little) : v.getFloat32(o, little);
  switch (bits) {
    case 8: return little ? (v.getUint8(o) - 128) / 128 : v.getInt8(o) / 128;  // WAV 8-bit is unsigned, AIFF signed
    case 16: return v.getInt16(o, little) / 32768;
    case 24: {
      const b0 = v.getUint8(o), b1 = v.getUint8(o + 1), b2 = v.getUint8(o + 2);
      const x = little ? b0 | (b1 << 8) | (((b2 << 24) >> 24) << 16) : b2 | (b1 << 8) | (((b0 << 24) >> 24) << 16);
      return x / 8388608;
    }
    case 32: return v.getInt32(o, little) / 2147483648;
    default: throw new Error(`unsupported bit depth ${bits}`);
  }
}

function deinterleave(v: DataView, offset: number, frames: number, channels: number, bits: number, float: boolean,
                      little: boolean, sampleRate: number): Audio {
  const bytes = bits / 8;
  const chans = Array.from({ length: channels }, () => new Float32Array(frames));
  let o = offset;
  for (let f = 0; f < frames; ++f)
    for (let c = 0; c < channels; ++c, o += bytes) chans[c][f] = readPcm(v, o, bits, float, little);
  return { sampleRate, channels: chans };
}

export function decodeWav(buf: ArrayBuffer): Audio {
  const v = new DataView(buf);
  if (buf.byteLength < 12 || tag(v, 0) !== "RIFF" || tag(v, 8) !== "WAVE") throw new Error("not a RIFF/WAVE file");
  let o = 12;
  let fmt: { float: boolean; channels: number; rate: number; bits: number } | null = null;
  while (o + 8 <= buf.byteLength) {
    const id = tag(v, o), size = v.getUint32(o + 4, true), body = o + 8;
    if (id === "fmt ") {
      let code = v.getUint16(body, true);
      if (code === 0xfffe && size >= 40) code = v.getUint16(body + 24, true);
      if (code !== 1 && code !== 3) throw new Error(`unsupported WAV format code ${code}`);
      fmt = { float: code === 3, channels: v.getUint16(body + 2, true), rate: v.getUint32(body + 4, true), bits: v.getUint16(body + 14, true) };
    } else if (id === "data") {
      if (!fmt) throw new Error("data chunk before fmt chunk");
      const bytes = Math.min(size, buf.byteLength - body);
      const frames = Math.floor(bytes / ((fmt.bits / 8) * fmt.channels));
      return deinterleave(v, body, frames, fmt.channels, fmt.bits, fmt.float, true, fmt.rate);
    }
    o = body + size + (size & 1);
  }
  throw new Error("no data chunk");
}

/** 80-bit IEEE extended (AIFF sample rate). */
function extended(v: DataView, o: number): number {
  const exp = v.getUint16(o) & 0x7fff, sign = v.getUint16(o) & 0x8000 ? -1 : 1;
  const hi = v.getUint32(o + 2), lo = v.getUint32(o + 6);
  if (exp === 0 && hi === 0 && lo === 0) return 0;
  return sign * (hi * 2 ** (exp - 16383 - 31) + lo * 2 ** (exp - 16383 - 63));
}

export function decodeAiff(buf: ArrayBuffer): Audio {
  const v = new DataView(buf);
  const kind = buf.byteLength >= 12 ? tag(v, 8) : "";
  if (tag(v, 0) !== "FORM" || (kind !== "AIFF" && kind !== "AIFC")) throw new Error("not an AIFF file");
  let o = 12;
  let comm: { channels: number; frames: number; bits: number; rate: number; float: boolean; little: boolean } | null = null;
  while (o + 8 <= buf.byteLength) {
    const id = tag(v, o), size = v.getUint32(o + 4), body = o + 8;
    if (id === "COMM") {
      let float = false, little = false;
      if (kind === "AIFC" && size >= 22) {
        const c = tag(v, body + 18);
        if (c === "sowt") little = true;
        else if (c === "fl32" || c === "FL32") float = true;
        else if (c !== "NONE") throw new Error(`unsupported AIFC compression ${c}`);
      }
      comm = { channels: v.getUint16(body), frames: v.getUint32(body + 2), bits: v.getUint16(body + 6), rate: extended(v, body + 8), float, little };
    } else if (id === "SSND") {
      if (!comm) throw new Error("SSND before COMM");
      const offset = v.getUint32(body);
      const start = body + 8 + offset;
      const frames = Math.min(comm.frames, Math.floor((buf.byteLength - start) / ((comm.bits / 8) * comm.channels)));
      return deinterleave(v, start, frames, comm.channels, comm.float ? 32 : comm.bits, comm.float, comm.little, comm.rate);
    }
    o = body + size + (size & 1);
  }
  throw new Error("no SSND chunk");
}

/** WAV or AIFF by content. */
export function decodeAudio(buf: ArrayBuffer): Audio {
  const v = new DataView(buf);
  if (buf.byteLength >= 12 && tag(v, 0) === "RIFF") return decodeWav(buf);
  if (buf.byteLength >= 12 && tag(v, 0) === "FORM") return decodeAiff(buf);
  throw new Error("unsupported audio format (WAV and AIFF so far)");
}
