/**
 * MP3 decoding with mpg123 compiled to WebAssembly (npm `mpg123-decoder` 1.0.3, MIT wrapper;
 * mpg123 itself is LGPL-2.1, see THIRD_PARTY_NOTICES.md). Only the plain decoder is used, not the
 * web-worker variant (no Worker in UXP; tree-shaken out of the bundle). mpg123 honours the
 * encoder delay in the LAME/Info header, so hit times match the original audio (first kick at
 * 0.2508 s in both the WAV and its MP3).
 *
 * The file is fed in chunks and control is given back between chunks (`yieldFn`), because one
 * long synchronous decode blocked Premiere's panel runtime (owner's crash, 2026-11-20).
 */
import { MPEGDecoder } from "mpg123-decoder";
import type { Audio } from "./audio";

export interface Mp3Progress {
  /** 0..1 of the input bytes. */
  fraction: number;
  /** ms until the decoder was ready (WebAssembly compiled). */
  readyMs: number;
}

export async function decodeMp3(buf: ArrayBuffer, opts: {
  chunkBytes?: number;
  onProgress?: (p: Mp3Progress) => void;
  yieldFn?: () => Promise<void>;
  isCancelled?: () => boolean;
} = {}): Promise<Audio> {
  const chunk = opts.chunkBytes ?? 64 * 1024;
  const yieldFn = opts.yieldFn ?? (() => new Promise<void>((r) => setTimeout(r, 0)));
  const t0 = Date.now();
  const d = new MPEGDecoder();
  await d.ready;
  const readyMs = Date.now() - t0;
  const parts: Float32Array[][] = [];
  let rate = 0, total = 0;
  try {
    const bytes = new Uint8Array(buf);
    for (let o = 0; o < bytes.length; o += chunk) {
      if (opts.isCancelled?.()) throw new Error("cancelled");
      const r = d.decode(bytes.subarray(o, Math.min(bytes.length, o + chunk)));
      if (r.samplesDecoded) {
        rate = r.sampleRate;
        total += r.samplesDecoded;
        parts.push(r.channelData.map((c) => c.slice(0, r.samplesDecoded)));
      }
      opts.onProgress?.({ fraction: Math.min(1, (o + chunk) / bytes.length), readyMs });
      await yieldFn();
    }
  } finally {
    d.free();
  }
  if (!total) throw new Error("no MP3 frames decoded");
  const nch = parts[0].length;
  const channels = Array.from({ length: nch }, () => new Float32Array(total));
  let at = 0;
  for (const p of parts) {
    for (let c = 0; c < nch; ++c) channels[c].set(p[c], at);
    at += p[0].length;
  }
  return { sampleRate: rate, channels };
}
