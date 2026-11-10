import { describe, expect, it } from "vitest";
import { KNOWN_RATES } from "../src/domain/frameMath";
import { buildPlan, staleReason, totalFrames, videoTrackLabel } from "../src/domain/rangePlan";
import { clip, snapshot } from "./helpers";

describe("range planning", () => {
  it("maps UI label V1 to API index 0", () => {
    expect(videoTrackLabel(0)).toBe("V1");
    expect(videoTrackLabel(2)).toBe("V3");
    expect(() => videoTrackLabel(-1)).toThrow();
  });

  it("user story: A [0,5s), gap, B [8s,12s), C — 3 jobs, gap ignored, sorted by start", () => {
    const plan = buildPlan(
      snapshot([clip(2, "C.mov", 300, 350), clip(0, "A.mov", 0, 125), clip(1, "B.mov", 200, 300)]),
    );
    expect(plan.jobs.map((j) => j.guideClipName)).toEqual(["A.mov", "B.mov", "C.mov"]);
    expect(plan.jobs.map((j) => j.expectedFrames)).toEqual([125, 100, 50]);
    expect(plan.jobs.map((j) => j.defaultBaseName)).toEqual(["001_A", "002_B", "003_C"]);
    const tpf = KNOWN_RATES["25"];
    expect(plan.jobs[1].startTick).toBe((200n * tpf).toString());
    expect(plan.jobs[1].endTick).toBe((300n * tpf).toString());
    expect(plan.jobs.every((j) => j.includeByDefault && j.warnings.length === 0)).toBe(true);
  });

  it("adjacent clips (even same source) stay separate jobs with touching ranges", () => {
    const plan = buildPlan(snapshot([clip(0, "A", 0, 50), clip(1, "A", 50, 225), clip(2, "A", 225, 250)]));
    expect(plan.jobs).toHaveLength(3);
    expect(plan.jobs[0].endTick).toBe(plan.jobs[1].startTick);
    expect(plan.jobs.map((j) => j.expectedFrames)).toEqual([50, 175, 25]);
    expect(totalFrames(plan.jobs)).toBe(250);
  });

  it("differing durations (2s, 7s, 1s) at 23.976", () => {
    const r = "23.976";
    const plan = buildPlan(snapshot([clip(0, "a", 0, 48, r), clip(1, "b", 48, 216, r), clip(2, "c", 216, 240, r)], r));
    expect(plan.jobs.map((j) => j.expectedFrames)).toEqual([48, 168, 24]);
  });

  it("skips zero-length clips and reports them", () => {
    const plan = buildPlan(snapshot([clip(0, "z", 10, 10), clip(1, "ok", 10, 20)]));
    expect(plan.jobs[0].skippable).toBe(true);
    expect(plan.jobs[0].includeByDefault).toBe(false);
    expect(plan.jobs[0].warnings[0].code).toBe("ZERO_DURATION");
  });

  it("excludes disabled clips by default, includes on option", () => {
    const s = snapshot([clip(0, "d", 0, 10, "25", { disabled: true }), clip(1, "e", 10, 20)]);
    expect(buildPlan(s).jobs[0].includeByDefault).toBe(false);
    expect(buildPlan(s).jobs[0].warnings.map((w) => w.code)).toContain("DISABLED_CLIP");
    expect(buildPlan(s, { includeDisabledClips: true }).jobs[0].includeByDefault).toBe(true);
  });

  it("flags transitions overlapping a clip without shifting boundaries", () => {
    const tpf = KNOWN_RATES["25"];
    const s = snapshot([clip(0, "a", 0, 50), clip(1, "b", 50, 100), clip(2, "c", 120, 140)], "25", {
      transitions: [{ startTicks: (45n * tpf).toString(), endTicks: (55n * tpf).toString() }],
      transitionCount: 1,
    });
    const plan = buildPlan(s);
    expect(plan.jobs[0].warnings.map((w) => w.code)).toContain("TRANSITION_AT_BOUNDARY");
    expect(plan.jobs[1].warnings.map((w) => w.code)).toContain("TRANSITION_AT_BOUNDARY");
    expect(plan.jobs[2].warnings).toHaveLength(0);
    expect(plan.jobs[0].endTick).toBe((50n * tpf).toString());
  });

  it("flags non-frame-aligned boundaries and keeps them unchanged", () => {
    const c = clip(0, "odd", 0, 10);
    c.endTicks = (BigInt(c.endTicks) + 7n).toString();
    const plan = buildPlan(snapshot([c]));
    expect(plan.jobs[0].warnings.map((w) => w.code)).toContain("NOT_FRAME_ALIGNED");
    expect(plan.jobs[0].endTick).toBe(c.endTicks);
    expect(plan.jobs[0].expectedFrames).toBe(11);
  });

  it("warns for hidden guide track and adjustment-layer guides", () => {
    const s = snapshot([clip(0, "adj", 0, 10, "25", { isAdjustmentLayer: true })], "25", {
      track: { index: 2, name: "", muted: true },
    });
    const plan = buildPlan(s);
    expect(plan.trackWarnings[0].code).toBe("MUTED_GUIDE_TRACK");
    expect(plan.trackWarnings[0].message).toContain("V3");
    expect(plan.jobs[0].warnings[0].code).toBe("ADJUSTMENT_LAYER_AS_GUIDE");
  });

  it("empty track -> no jobs", () => {
    expect(buildPlan(snapshot([])).jobs).toHaveLength(0);
  });

  it("plans are immutable", () => {
    const plan = buildPlan(snapshot([clip(0, "a", 0, 10)]));
    expect(Object.isFrozen(plan.jobs[0])).toBe(true);
    expect(() => ((plan.jobs as unknown as unknown[]).push(1))).toThrow();
  });

  it("handles hundreds of clips quickly", () => {
    const clips = Array.from({ length: 1000 }, (_, i) => clip(i, `c${i}`, i * 10, i * 10 + 10));
    const t = performance.now();
    const plan = buildPlan(snapshot(clips.reverse()));
    expect(plan.jobs).toHaveLength(1000);
    expect(plan.jobs[999].defaultBaseName).toBe("1000_c999");
    expect(performance.now() - t).toBeLessThan(1000);
  });
});

describe("stale scan detection", () => {
  const base = snapshot([clip(0, "a", 0, 10), clip(1, "b", 10, 20)]);
  const plan = buildPlan(base);
  it("unchanged -> null", () => expect(staleReason(plan, snapshot([clip(0, "a", 0, 10), clip(1, "b", 10, 20)]))).toBeNull());
  it("trimmed clip", () => expect(staleReason(plan, snapshot([clip(0, "a", 0, 9), clip(1, "b", 10, 20)]))).toBe("CLIPS_CHANGED"));
  it("renamed clip", () => expect(staleReason(plan, snapshot([clip(0, "x", 0, 10), clip(1, "b", 10, 20)]))).toBe("CLIPS_CHANGED"));
  it("disabled clip", () => expect(staleReason(plan, snapshot([clip(0, "a", 0, 10, "25", { disabled: true }), clip(1, "b", 10, 20)]))).toBe("CLIPS_CHANGED"));
  it("other sequence", () => expect(staleReason(plan, { ...base, sequenceGuid: "other" })).toBe("SEQUENCE_CHANGED"));
  it("other track", () => expect(staleReason(plan, { ...base, track: { ...base.track, index: 1 } })).toBe("TRACK_CHANGED"));
  it("other project", () => expect(staleReason(plan, { ...base, projectPath: "/x.prproj" })).toBe("PROJECT_CHANGED"));
  it("frame rate", () => expect(staleReason(plan, { ...base, ticksPerFrame: "1" })).toBe("FRAME_RATE_CHANGED"));
});
