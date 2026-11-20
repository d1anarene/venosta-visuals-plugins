import { describe, expect, it } from "vitest";

describe("TextDecoder stand-in", () => {
  it("decodes UTF-8 like the real one", async () => {
    const real = globalThis.TextDecoder;
    (globalThis as { TextDecoder?: unknown }).TextDecoder = undefined;
    try {
      await import("../src/panel/polyfills");
      const Stand = globalThis.TextDecoder as unknown as { new (): { decode(b: Uint8Array): string } };
      const text = "Größe: 120 % – ok 🎵";
      expect(new Stand().decode(new TextEncoder().encode(text))).toBe(text);
    } finally {
      globalThis.TextDecoder = real;
    }
  });
});
