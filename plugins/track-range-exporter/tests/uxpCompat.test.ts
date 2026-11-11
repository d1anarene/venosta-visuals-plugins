import { readdirSync, readFileSync, statSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { utf8Length } from "../src/domain/naming";

// APIs / elements that Premiere's UXP runtime does not provide (found in host testing on
// Premiere 27.0 / UXP 9.4). Panel code must not use them.
const FORBIDDEN_CODE = [/\bTextEncoder\b/, /\bTextDecoder\b/, /\.toggleAttribute\(/, /\.normalize\(\s*"NF/];
const FORBIDDEN_HTML = [/<details/i, /<summary/i, /<section/i, /<header/i, /<footer/i];

function files(dir: string, ext: RegExp): string[] {
  return readdirSync(dir).flatMap((f) => {
    const p = join(dir, f);
    return statSync(p).isDirectory() ? files(p, ext) : ext.test(f) ? [p] : [];
  });
}

describe("UXP runtime compatibility", () => {
  it("source avoids APIs missing in UXP (except guarded uses)", () => {
    for (const f of [...files("src", /\.ts$/), ...files("ame-companion/src", /\.ts$/)]) {
      const code = readFileSync(f, "utf8")
        .split("\n")
        .filter((l) => !l.trim().startsWith("//") && !l.trim().startsWith("/*") && !l.trim().startsWith("*") && !l.includes("typeof s.normalize"));
      for (const re of FORBIDDEN_CODE) {
        const hit = code.find((l) => re.test(l));
        expect(hit, `${f} uses ${re}`).toBeUndefined();
      }
    }
  });
  it("panel HTML avoids elements UXP does not lay out", () => {
    for (const f of ["public/index.html", "ame-companion/public/index.html"]) {
      const html = readFileSync(f, "utf8");
      for (const re of FORBIDDEN_HTML) expect(re.test(html), `${f} contains ${re}`).toBe(false);
    }
  });
  it("utf8Length matches the platform encoder", () => {
    for (const s of ["", "abc", "Café", "日本語", "😀 film", "Ä".repeat(300), "\u{10FFFF}"])
      expect(utf8Length(s)).toBe(Buffer.byteLength(s, "utf8"));
  });
});
