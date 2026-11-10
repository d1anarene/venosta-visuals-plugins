/**
 * Route B — "Media Encoder companion": runs INSIDE Adobe Media Encoder (UXP, AME 27.0+ beta API).
 *
 * For each job in the manifest:
 *   opts = new RenderOptions()
 *   opts.setSequenceGUID(manifest.sequenceGuid)
 *   opts.setCustomInAndOutPoints(TickTime.createWithTicks(start), TickTime.createWithTicks(out))
 *   RenderQueue.enqueueFile(projectPath, presetPath, outputPath, opts)
 *
 * Each AME job carries its own range, so the Premiere sequence is never modified.
 * API references: AME docs render-queue (enqueueFile, getProjectItemGUIDs), render-options
 * (setSequenceGUID, setCustomInAndOutPoints) and the official sample
 * uxp-media-encoder-plugin-samples/.../renderOptions.ts, which passes RenderOptions as the 4th
 * argument of renderFile/enqueueFile.
 */
import { parseTicks } from "../domain/frameMath";
import type { JobManifest } from "./handoffManifest";
import { hostOutPoint, type JobUpdate, type QueueRunResult } from "./queueAdapter";

/** Minimal surface of AME's `require("mediaencoder")` that we use; injected for tests. */
export interface AmeHost {
  enqueue(args: {
    projectPath: string;
    presetPath: string;
    outputPath: string;
    sequenceGuid: string;
    startTicks: string;
    outTicks: string;
  }): Promise<{ ok: boolean; jobId?: string; message?: string }>;
  /** Root-level sequence GUIDs of the saved project, or null if the call is unavailable. */
  projectSequenceGuids(projectPath: string): Promise<string[] | null>;
  fileExists(path: string): Promise<boolean>;
}

export interface AmeRunOptions {
  onUpdate: (u: JobUpdate) => void;
  isCancelled: () => boolean;
}

export async function preflightManifest(host: AmeHost, m: JobManifest): Promise<string[]> {
  const problems: string[] = [];
  if (!(await host.fileExists(m.projectPath))) {
    problems.push(`Project file not found: ${m.projectPath}. Save the project in Premiere first.`);
  }
  if (!(await host.fileExists(m.presetPath))) problems.push(`Preset not found: ${m.presetPath}`);
  for (const j of m.jobs) {
    if (await host.fileExists(j.outputPath)) {
      problems.push(`Output already exists (will not overwrite): ${j.outputPath}`);
    }
  }
  if (problems.length === 0) {
    const guids = await host.projectSequenceGuids(m.projectPath);
    if (guids && guids.length > 0 && !guids.some((g) => g.toLowerCase() === m.sequenceGuid.toLowerCase())) {
      // getProjectItemGUIDs only lists root-level items, so a sequence inside a bin is
      // not listed. This is a warning, not a blocker.
      problems.push(
        "WARNING: the sequence was not found at the project root. If it is inside a bin this is " +
          "expected; AME will still be asked for it by GUID.",
      );
    }
  }
  return problems;
}

export async function runAmeQueue(
  host: AmeHost,
  m: JobManifest,
  opts: AmeRunOptions,
): Promise<QueueRunResult> {
  const result: QueueRunResult = { queued: 0, failed: 0, cancelled: 0, fatalErrors: [] };
  const tpf = parseTicks(m.ticksPerFrame);
  for (const job of m.jobs) {
    if (opts.isCancelled()) {
      opts.onUpdate({ jobId: job.id, status: "cancelled" });
      result.cancelled++;
      continue;
    }
    opts.onUpdate({ jobId: job.id, status: "adding" });
    try {
      const out = hostOutPoint(parseTicks(job.endTicks), tpf, m.outPointConvention);
      const r = await host.enqueue({
        projectPath: m.projectPath,
        presetPath: m.presetPath,
        outputPath: job.outputPath,
        sequenceGuid: m.sequenceGuid,
        startTicks: job.startTicks,
        outTicks: out.toString(),
      });
      if (!r.ok) throw new Error(r.message || "Media Encoder rejected the job");
      opts.onUpdate({ jobId: job.id, status: "queued", hostJobId: r.jobId, message: r.message });
      result.queued++;
    } catch (e) {
      opts.onUpdate({ jobId: job.id, status: "failed", message: e instanceof Error ? e.message : String(e) });
      result.failed++;
    }
  }
  return result;
}
