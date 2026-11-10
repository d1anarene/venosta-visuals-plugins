import { describe, expect, it } from "vitest";
import { cleanBaseName, joinPath, normaliseExtension, resolveFileNames, validateBaseName } from "../src/domain/naming";

describe("naming", () => {
  it("cleans clip names", () => {
    expect(cleanBaseName("A.mov")).toBe("A");
    expect(cleanBaseName('in:valid/na*me?.MXF')).toBe("in_valid_na_me_");
    expect(cleanBaseName("  ..hidden.  ")).toBe("hidden");
    expect(cleanBaseName("")).toBe("clip");
    expect(cleanBaseName("CON")).toBe("CON_");
    expect(cleanBaseName("Café ☕ Szene")).toBe("Café ☕ Szene");
    expect(cleanBaseName("Cafe\u0301")).toBe("Café"); // NFC
  });

  it("truncates long names on code point boundaries", () => {
    const long = "😀".repeat(100); // 400 bytes
    const out = cleanBaseName(long);
    expect(new TextEncoder().encode(out).length).toBeLessThanOrEqual(180);
    expect(out).toBe("😀".repeat(45));
  });

  it("validates user edits", () => {
    expect(validateBaseName("ok name")).toBeNull();
    expect(validateBaseName("bad/name")).not.toBeNull();
    expect(validateBaseName("bad/name")).not.toBeNull(); // regex lastIndex reset
    expect(validateBaseName("")).not.toBeNull();
    expect(validateBaseName("trailing.")).not.toBeNull();
  });

  it("extensions come from the preset, not hardcoded", () => {
    expect(normaliseExtension(".MOV")).toBe("MOV");
    expect(normaliseExtension("mxf")).toBe("mxf");
    expect(() => normaliseExtension("../x")).toThrow();
  });

  it("resolves duplicates in queue and against existing files, case-insensitively", () => {
    const r = resolveFileNames(
      [
        { id: "1", baseName: "001_A" },
        { id: "2", baseName: "shot" },
        { id: "3", baseName: "SHOT" },
        { id: "4", baseName: "shot" },
      ],
      "mov",
      ["001_a.MOV", "shot (2).mov"],
    );
    expect(r.map((x) => x.fileName)).toEqual(["001_A (2).mov", "shot.mov", "SHOT (3).mov", "shot (4).mov"]);
    expect(r.map((x) => x.renamed)).toEqual([true, false, true, true]);
  });

  it("joins paths for both platforms", () => {
    expect(joinPath("/Users/me/out", "a.mp4")).toBe("/Users/me/out/a.mp4");
    expect(joinPath("/Users/me/out/", "a.mp4")).toBe("/Users/me/out/a.mp4");
    expect(joinPath("C:\\Exports", "a.mp4")).toBe("C:\\Exports\\a.mp4");
  });
});
