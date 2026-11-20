/**
 * MP3 decoding with mpg123 compiled to WebAssembly (npm `mpg123-decoder` 1.0.3, MIT wrapper;
 * mpg123 itself is LGPL-2.1, see THIRD_PARTY_NOTICES.md). Only the plain decoder is imported, not
 * the web-worker variant (no Worker in UXP; tree-shaken out of the bundle). mpg123 honours the encoder delay in the LAME/Info
 * header, so hit times match the original audio (measured: first kick at 0.2508 s in both the
 * WAV and its MP3, 3 min of MP3 in about 0.4 s in Node).
 */
import { MPEGDecoder } from "mpg123-decoder";
import type { Audio } from "./audio";

export async function decodeMp3(buf: ArrayBuffer): Promise<Audio> {
  const d = new MPEGDecoder();
  await d.ready;
  try {
    const r = d.decode(new Uint8Array(buf));
    if (!r || !r.samplesDecoded) throw new Error("no MP3 frames decoded");
    return { sampleRate: r.sampleRate, channels: r.channelData.map((c) => c.slice(0, r.samplesDecoded)) };
  } finally {
    d.free();
  }
}
