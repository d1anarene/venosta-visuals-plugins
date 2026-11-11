import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { KNOWN_RATES } from "../src/domain/frameMath";
import {
  eprFloat, fourCC, makeUuid, paramType, patchPreset, presetDisplayName, readEprInfo, readParam,
} from "../src/domain/eprPreset";
import { chooseTemplate, EXPORT_TARGETS, findTarget, FPS_STEPS, fpsTicksForStep } from "../src/domain/exportTargets";

const tpl = readFileSync("tests/fixtures/sample-h264.epr", "utf8");

describe(".epr reading", () => {
  it("decodes four-character codes", () => {
    expect(fourCC(1836082994)).toBe("mpg2"); // from Adobe's sample preset
    expect(fourCC(1211250228)).toBe("H264");
    expect(fourCC(0)).toBe("");
  });
  it("reads name, file type and params", () => {
    expect(readEprInfo(tpl)).toEqual({ name: "Match Source - High bitrate", fileType: "H264" });
    expect(readEprInfo("<html/>")).toBeNull();
    expect(presetDisplayName("Plain name")).toBe("Plain name");
    expect(readParam(tpl, "ADBEVideoFPS")).toBe("10160640000");
    expect(readParam(tpl, "ADBEVideoMatchSource")).toBeUndefined(); // no value
    expect(paramType(tpl, "ADBEVideoTargetBitrate")).toBe(3);
  });
});

describe(".epr patching", () => {
  const base = { name: "TRE YouTube", presetId: "11111111-2222-4333-8444-555555555555" };
  it("changes only existing parameters, in their own format", () => {
    const r = patchPreset(tpl, {
      ...base, width: 1080, height: 1920, fpsTicks: KNOWN_RATES["23.976"].toString(), bitrateMbps: 16,
      audio: true, audioBitrateKbps: 256, audioSampleRate: 44100,
    });
    expect(readParam(r.xml, "ADBEVideoWidth")).toBe("1080");
    expect(readParam(r.xml, "ADBEVideoHeight")).toBe("1920");
    expect(readParam(r.xml, "ADBEVideoFPS")).toBe("10594584000");
    expect(readParam(r.xml, "ADBEVideoTargetBitrate")).toBe("16.");
    expect(readParam(r.xml, "ADBEVideoMaxBitrate")).toBe("20.");  // raised to 1.25x target
    expect(readParam(r.xml, "ADBEAudioBitrate")).toBe("256");
    expect(readParam(r.xml, "ADBEAudioRatePerSecond")).toBe("44100");
    expect(r.xml).toContain("<PresetName>TRE YouTube</PresetName>");
    expect(r.xml).toContain(`<PresetID>${base.presetId}</PresetID>`);
    expect(r.skipped).toEqual([]);
    // untouched parts stay byte-identical
    expect(r.xml.split("ADBEVideoMatchSource")[0].length).toBeGreaterThan(0);
    expect(r.xml.length - tpl.length).toBeLessThan(200);
  });
  it("turns audio off without touching audio params", () => {
    const r = patchPreset(tpl, { ...base, audio: false, audioBitrateKbps: 128 });
    expect(r.xml).toContain("<DoAudio>false</DoAudio>");
    expect(readParam(r.xml, "ADBEAudioBitrate")).toBe("320");
  });
  it("reports parameters the template does not have instead of inventing them", () => {
    const noBitrate = tpl.replace(/<ExporterParam[^>]*>\s*<ParamValue>10\.<\/ParamValue>[\s\S]*?<\/ExporterParam>/, "");
    const r = patchPreset(noBitrate, { ...base, bitrateMbps: 30 });
    expect(r.skipped).toContain("bitrate 30 Mbps");
    expect(readParam(r.xml, "ADBEVideoTargetBitrate")).toBeUndefined();
  });
  it("escapes names", () => {
    const r = patchPreset(tpl, { ...base, name: "A & <B>" });
    expect(r.xml).toContain("<PresetName>A &amp; &lt;B&gt;</PresetName>");
  });
  it("formats floats like Media Encoder", () => {
    expect(eprFloat(15)).toBe("15.");
    expect(eprFloat(18.5)).toBe("18.5");
  });
  it("makes v4 UUIDs without crypto", () => {
    const u = makeUuid();
    expect(u).toMatch(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
    expect(makeUuid()).not.toBe(u);
  });
});

describe("export destinations", () => {
  const installed = [
    { path: "/a/h264-match.epr", name: "Match Source - High bitrate", fileType: "H264" },
    { path: "/a/h264-yt.epr", name: "YouTube 1080p Full HD", fileType: "H264" },
    { path: "/a/hevc.epr", name: "Match Source - High bitrate", fileType: "HEVC" },
    { path: "/a/pr422hq.epr", name: "Apple ProRes 422 HQ", fileType: "MooV" },
    { path: "/a/pr422.epr", name: "Apple ProRes 422", fileType: "MooV" },
    { path: "/a/pr4444xq.epr", name: "Apple ProRes 4444 XQ", fileType: "MooV" },
    { path: "/a/pr4444.epr", name: "Apple ProRes 4444", fileType: "MooV" },
  ];
  it("has unique ids and sane sliders", () => {
    const ids = EXPORT_TARGETS.map((t) => t.id);
    expect(new Set(ids).size).toBe(ids.length);
    for (const t of EXPORT_TARGETS) {
      if (t.bitrate) expect(t.bitrate.min <= t.bitrate.default && t.bitrate.default <= t.bitrate.max).toBe(true);
      if (t.audioBitrates) expect(t.audioBitrates).toContain(t.defaultAudioBitrate);
    }
  });
  it("matches templates by format and name", () => {
    expect(chooseTemplate(findTarget("youtube-1080")!, installed)?.path).toBe("/a/h264-match.epr");
    expect(chooseTemplate(findTarget("hevc-match")!, installed)?.path).toBe("/a/hevc.epr");
    expect(chooseTemplate(findTarget("prores-422hq")!, installed)?.path).toBe("/a/pr422hq.epr");
    expect(chooseTemplate(findTarget("prores-422")!, installed)?.path).toBe("/a/pr422.epr");
    expect(chooseTemplate(findTarget("prores-4444")!, installed)?.path).toBe("/a/pr4444.epr");
    expect(chooseTemplate(findTarget("dnxhr-hqx")!, installed)).toBeNull(); // not installed -> reported
  });
  it("accepts translated names by format for H.264", () => {
    const german = [{ path: "/de.epr", name: "Quelle anpassen – Hohe Bitrate", fileType: "H264" }];
    expect(chooseTemplate(findTarget("youtube-1080")!, german)?.path).toBe("/de.epr");
  });
  it("maps fps slider steps to exact ticks", () => {
    expect(fpsTicksForStep(0, "10594584000")).toBe("10594584000"); // sequence rate
    expect(fpsTicksForStep(FPS_STEPS.findIndex((s) => s.key === "25"), "1")).toBe("10160640000");
    expect(fpsTicksForStep(99, "1")).toBe(KNOWN_RATES["60"].toString());
  });
});
