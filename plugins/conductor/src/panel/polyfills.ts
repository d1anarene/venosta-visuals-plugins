/**
 * Minimal stand-ins for browser APIs that Premiere's UXP lacks but the MP3 decoder (Emscripten
 * runtime) expects: TextDecoder (UTF-8 only; used for its diagnostic strings) and
 * performance.now. Installed only when missing. Host result 2026-11-20: "TextDecoder is not
 * defined" in Premiere 27 on macOS.
 */
/* eslint-disable @typescript-eslint/no-explicit-any */
const g = globalThis as any;

if (typeof g.TextDecoder === "undefined") {
  g.TextDecoder = class {
    decode(input?: ArrayBufferView | ArrayBuffer): string {
      if (!input) return "";
      const b = input instanceof ArrayBuffer ? new Uint8Array(input) : new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
      let out = "";
      for (let i = 0; i < b.length; ) {
        const c = b[i++];
        let cp: number;
        if (c < 0x80) cp = c;
        else if (c >= 0xc0 && c < 0xe0) cp = ((c & 0x1f) << 6) | (b[i++] & 0x3f);
        else if (c >= 0xe0 && c < 0xf0) cp = ((c & 0x0f) << 12) | ((b[i++] & 0x3f) << 6) | (b[i++] & 0x3f);
        else if (c >= 0xf0) cp = ((c & 0x07) << 18) | ((b[i++] & 0x3f) << 12) | ((b[i++] & 0x3f) << 6) | (b[i++] & 0x3f);
        else cp = 0xfffd;
        out += String.fromCodePoint(cp);
      }
      return out;
    }
  };
}

if (typeof g.performance === "undefined") g.performance = {};
if (typeof g.performance.now !== "function") g.performance.now = () => Date.now();
export {};
