/**
 * Real AME host built on `require("mediaencoder")` (AME UXP 27.0+, public beta).
 * The AME UXP API ships no TypeScript declarations, so the surface used here is typed locally
 * from the official docs and the official sample (uxp-media-encoder-plugin-samples).
 */
import type { AmeHost } from "../../src/export/ameAdapter";
import { pathExists } from "../../src/ui/fileSystem";

// eslint-disable-next-line @typescript-eslint/no-require-imports
declare const require: (id: string) => any;
/* eslint-disable @typescript-eslint/no-explicit-any */

function ame(): any {
  return require("mediaencoder");
}

function newRenderOptions(app: any): any {
  // Docs show `new app.RenderOptions()`, the official sample calls `app.RenderOptions()`.
  try {
    return new app.RenderOptions();
  } catch {
    return app.RenderOptions();
  }
}

export const realAmeHost: AmeHost = {
  async enqueue(a) {
    const app = ame();
    const opts = newRenderOptions(app);
    opts.setSequenceGUID(a.sequenceGuid);
    opts.setCustomInAndOutPoints(app.TickTime.createWithTicks(a.startTicks), app.TickTime.createWithTicks(a.outTicks));
    const res = await app.RenderQueue.enqueueFile(a.projectPath, a.presetPath, a.outputPath, opts);
    // Documented result: { jobGroupId, jobId, message, outputPath, result: 0 = success, 1 = error }
    const ok = !!res && res.result === 0;
    return { ok, jobId: res?.jobId, message: res?.message };
  },
  async projectSequenceGuids(projectPath) {
    try {
      const res = await ame().RenderQueue.getProjectItemGUIDs(projectPath);
      return Array.isArray(res?.guids) ? res.guids.map(String) : null;
    } catch {
      return null;
    }
  },
  fileExists: pathExists,
};

/** Job status names from RenderJob.STATUS_* constants. */
export function jobStatus(jobId: string): string | null {
  try {
    const app = ame();
    const job = app.RenderQueue.getJob(jobId);
    if (!job) return null;
    const s = job.getStatus();
    const J = app.RenderJob ?? job;
    const map: Array<[string, string]> = [
      ["STATUS_WAITING", "queued"], ["STATUS_ENCODING", "rendering"], ["STATUS_DONE", "done"],
      ["STATUS_DONE_WARNING", "done (warnings)"], ["STATUS_FAILED", "failed"], ["STATUS_PAUSED", "paused"],
      ["STATUS_STOPPED", "stopped"], ["STATUS_SKIPPED", "skipped"],
    ];
    for (const [k, label] of map) if (J[k] !== undefined && J[k] === s) return label;
    return String(s);
  } catch {
    return null;
  }
}

export async function startQueue(): Promise<void> {
  await ame().RenderQueue.start();
}
