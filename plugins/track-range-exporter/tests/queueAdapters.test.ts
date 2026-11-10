import { describe, expect, it } from "vitest";
import { KNOWN_RATES } from "../src/domain/frameMath";
import { buildPlan } from "../src/domain/rangePlan";
import { preflightManifest, runAmeQueue, type AmeHost } from "../src/export/ameAdapter";
import { MANIFEST_FORMAT, ManifestError, parseManifest, serializeManifest, type JobManifest } from "../src/export/handoffManifest";
import { runPremiereDirectQueue, type InOutHost, type InOutPoints } from "../src/export/premiereEncoderAdapter";
import type { JobUpdate, QueueRequest } from "../src/export/queueAdapter";
import { clip, snapshot } from "./helpers";

const TPF = KNOWN_RATES["25"];
const plan = buildPlan(snapshot([clip(0, "A", 0, 50), clip(1, "B", 75, 250)]));
const requests: QueueRequest[] = plan.jobs.map((job, i) => ({
  job, outputPath: `/out/00${i + 1}.mp4`, presetPath: "/p.epr",
}));

class FakeSequence implements InOutHost {
  points: InOutPoints = { inTicks: "123", outTicks: "999999999999" };
  log: string[] = [];
  snapshotsAtQueue: InOutPoints[] = [];
  failQueueAt = -1;
  rejectSetAt = -1;
  private sets = 0;
  async getInOut() { return { ...this.points }; }
  async setInOut(p: InOutPoints, label: string) {
    this.sets++;
    if (this.sets - 1 === this.rejectSetAt) return false;
    this.points = { ...p };
    this.log.push(`set ${p.inTicks}-${p.outTicks} (${label})`);
    return true;
  }
  async queueInToOut(out: string) {
    if (this.snapshotsAtQueue.length === this.failQueueAt) throw new Error("AME not available");
    this.snapshotsAtQueue.push({ ...this.points });
    this.log.push(`queue ${out}`);
    return true;
  }
}

function collect() {
  const updates: JobUpdate[] = [];
  return { updates, onUpdate: (u: JobUpdate) => updates.push(u) };
}

describe("Premiere direct route (in/out per job)", () => {
  it("queues each job with its own range and restores the original in/out", async () => {
    const seq = new FakeSequence();
    const { updates, onUpdate } = collect();
    const r = await runPremiereDirectQueue(seq, requests, TPF, "exclusive", {
      onUpdate, isCancelled: () => false, verifyUnchanged: async () => null,
    });
    expect(r).toEqual({ queued: 2, failed: 0, cancelled: 0, fatalErrors: [] });
    expect(seq.snapshotsAtQueue).toEqual([
      { inTicks: "0", outTicks: (50n * TPF).toString() },
      { inTicks: (75n * TPF).toString(), outTicks: (250n * TPF).toString() },
    ]);
    expect(seq.points).toEqual({ inTicks: "123", outTicks: "999999999999" });
    expect(updates.filter((u) => u.status === "queued")).toHaveLength(2);
  });

  it("inclusive convention subtracts exactly one frame from the out point", async () => {
    const seq = new FakeSequence();
    await runPremiereDirectQueue(seq, requests.slice(0, 1), TPF, "inclusive", {
      onUpdate: () => {}, isCancelled: () => false, verifyUnchanged: async () => null,
    });
    expect(seq.snapshotsAtQueue[0].outTicks).toBe((49n * TPF).toString());
  });

  it("restores in/out when a job fails and stops the rest", async () => {
    const seq = new FakeSequence();
    seq.failQueueAt = 0;
    const { updates, onUpdate } = collect();
    const r = await runPremiereDirectQueue(seq, requests, TPF, "exclusive", {
      onUpdate, isCancelled: () => false, verifyUnchanged: async () => null,
    });
    expect(r.queued).toBe(0);
    expect(r.failed).toBe(1);
    expect(r.cancelled).toBe(1);
    expect(updates.find((u) => u.status === "failed")?.message).toContain("AME not available");
    expect(seq.points).toEqual({ inTicks: "123", outTicks: "999999999999" });
    expect(r.fatalErrors).toEqual([]);
  });

  it("restores in/out on user cancel between jobs", async () => {
    const seq = new FakeSequence();
    let n = 0;
    const r = await runPremiereDirectQueue(seq, requests, TPF, "exclusive", {
      onUpdate: () => {}, isCancelled: () => n++ > 0, verifyUnchanged: async () => null,
    });
    expect(r.queued).toBe(1);
    expect(r.cancelled).toBe(1);
    expect(seq.points).toEqual({ inTicks: "123", outTicks: "999999999999" });
  });

  it("stops when the timeline changes mid-run", async () => {
    const seq = new FakeSequence();
    let n = 0;
    const r = await runPremiereDirectQueue(seq, requests, TPF, "exclusive", {
      onUpdate: () => {}, isCancelled: () => false, verifyUnchanged: async () => (n++ > 0 ? "clips changed" : null),
    });
    expect(r.queued).toBe(1);
    expect(r.cancelled).toBe(1);
    expect(seq.points.inTicks).toBe("123");
  });

  it("refuses to queue when Premiere does not apply the in/out", async () => {
    const seq = new FakeSequence();
    seq.rejectSetAt = 0;
    const r = await runPremiereDirectQueue(seq, requests, TPF, "exclusive", {
      onUpdate: () => {}, isCancelled: () => false, verifyUnchanged: async () => null,
    });
    expect(r.queued).toBe(0);
    expect(seq.snapshotsAtQueue).toHaveLength(0);
  });

  it("reports loudly when the restore fails", async () => {
    const seq = new FakeSequence();
    const orig = seq.setInOut.bind(seq);
    seq.setInOut = async (p, label) => (label.includes("restore") ? false : orig(p, label));
    const r = await runPremiereDirectQueue(seq, requests, TPF, "exclusive", {
      onUpdate: () => {}, isCancelled: () => false, verifyUnchanged: async () => null,
    });
    expect(r.fatalErrors[0]).toContain("Could not restore");
  });

  it("does not touch the sequence when there is nothing to queue", async () => {
    const seq = new FakeSequence();
    await runPremiereDirectQueue(seq, [], TPF, "exclusive", {
      onUpdate: () => {}, isCancelled: () => false, verifyUnchanged: async () => null,
    });
    expect(seq.log).toEqual([]);
  });
});

function manifest(): JobManifest {
  return {
    format: MANIFEST_FORMAT, version: 1, createdAt: "2026-11-10T00:00:00Z",
    projectPath: "/Projects/test.prproj", sequenceGuid: "seq-guid-1", sequenceName: "Graded",
    presetPath: "/p.epr", ticksPerFrame: TPF.toString(), outPointConvention: "exclusive",
    jobs: requests.map((r) => ({
      id: r.job.id, ordinal: r.job.ordinal, clipName: r.job.guideClipName,
      startTicks: r.job.startTick, endTicks: r.job.endTick, expectedFrames: r.job.expectedFrames,
      outputPath: r.outputPath,
    })),
  };
}

describe("job file (Premiere -> AME handoff)", () => {
  it("round-trips", () => {
    expect(parseManifest(serializeManifest(manifest()))).toEqual(manifest());
  });
  it.each([
    ["not json", "{"],
    ["wrong format", JSON.stringify({ ...manifest(), format: "x" })],
    ["future version", JSON.stringify({ ...manifest(), version: 99 })],
    ["unsaved project", JSON.stringify({ ...manifest(), projectPath: "" })],
    ["not a prproj", JSON.stringify({ ...manifest(), projectPath: "/a.mov" })],
    ["no jobs", JSON.stringify({ ...manifest(), jobs: [] })],
    ["reversed range", JSON.stringify({ ...manifest(), jobs: [{ ...manifest().jobs[0], startTicks: "10", endTicks: "5" }] })],
    ["float ticks", JSON.stringify({ ...manifest(), jobs: [{ ...manifest().jobs[0], startTicks: "1.5" }] })],
    ["duplicate outputs", JSON.stringify({ ...manifest(), jobs: [manifest().jobs[0], { ...manifest().jobs[1], outputPath: "/OUT/001.mp4" }] })],
  ])("rejects %s", (_n, text) => {
    expect(() => parseManifest(text)).toThrow(ManifestError);
  });
});

describe("AME companion route (per-job custom in/out)", () => {
  function fakeAme(existing: string[] = ["/Projects/test.prproj", "/p.epr"]) {
    const calls: Parameters<AmeHost["enqueue"]>[0][] = [];
    const host: AmeHost = {
      enqueue: async (a) => { calls.push(a); return { ok: true, jobId: `job-${calls.length}` }; },
      projectSequenceGuids: async () => ["seq-guid-1"],
      fileExists: async (p) => existing.includes(p),
    };
    return { host, calls };
  }

  it("enqueues one AME job per range with the sequence GUID", async () => {
    const { host, calls } = fakeAme();
    const { updates, onUpdate } = collect();
    const r = await runAmeQueue(host, manifest(), { onUpdate, isCancelled: () => false });
    expect(r.queued).toBe(2);
    expect(calls.map((c) => [c.sequenceGuid, c.startTicks, c.outTicks, c.outputPath])).toEqual([
      ["seq-guid-1", "0", (50n * TPF).toString(), "/out/001.mp4"],
      ["seq-guid-1", (75n * TPF).toString(), (250n * TPF).toString(), "/out/002.mp4"],
    ]);
    expect(updates.filter((u) => u.status === "queued").map((u) => u.hostJobId)).toEqual(["job-1", "job-2"]);
  });

  it("marks a rejected job failed and continues", async () => {
    const { host } = fakeAme();
    let n = 0;
    host.enqueue = async () => (n++ === 0 ? { ok: false, message: "bad preset" } : { ok: true });
    const { updates, onUpdate } = collect();
    const r = await runAmeQueue(host, manifest(), { onUpdate, isCancelled: () => false });
    expect([r.queued, r.failed]).toEqual([1, 1]);
    expect(updates.find((u) => u.status === "failed")?.message).toBe("bad preset");
  });

  it("preflight blocks missing project/preset and existing outputs", async () => {
    const { host } = fakeAme(["/out/001.mp4"]);
    const p = await preflightManifest(host, manifest());
    expect(p.join("\n")).toMatch(/Project file not found/);
    expect(p.join("\n")).toMatch(/Preset not found/);
    expect(p.join("\n")).toMatch(/will not overwrite/);
  });

  it("preflight passes for a valid manifest", async () => {
    expect(await preflightManifest(fakeAme().host, manifest())).toEqual([]);
  });
});
