/**
 * Route A — "Premiere direct": one click inside Premiere.
 *
 * For each job, serially:
 *   1. set the ACTIVE sequence's in/out to the job range (one undoable transaction),
 *   2. read the in/out back and verify it is exactly what we set,
 *   3. call EncoderManager.exportSequence(sequence, QUEUE_TO_AME, file, preset, exportFull=false)
 *      and wait for it to resolve before touching in/out again.
 * Finally (always — success, error or cancel) the user's original in/out is restored and verified.
 *
 * This relies on Premiere capturing the sequence state at the moment a job is handed to AME.
 * That is how Premiere's "Queue" export behaves for users, but it is NOT yet verified for the
 * UXP API in a real host by this project — see docs/TECHNICAL_DECISIONS.md and PROGRESS.md.
 *
 * The host is abstracted behind `InOutHost` so the ordering/restore logic is unit-tested.
 */
import { parseTicks } from "../domain/frameMath";
import {
  hostOutPoint,
  type OutPointConvention,
  type QueueRequest,
  type QueueRunOptions,
  type QueueRunResult,
} from "./queueAdapter";

export interface InOutPoints {
  inTicks: string;
  outTicks: string;
}

export interface InOutHost {
  getInOut(): Promise<InOutPoints>;
  /** Sets both points in one undoable transaction. Resolves false if Premiere refused. */
  setInOut(points: InOutPoints, undoLabel: string): Promise<boolean>;
  /** Queue the active sequence's in/out range to AME. Resolves true when Premiere accepted it. */
  queueInToOut(outputPath: string, presetPath: string): Promise<boolean>;
}

function sameTicks(a: string, b: string): boolean {
  try {
    return parseTicks(a) === parseTicks(b);
  } catch {
    return false;
  }
}

export async function runPremiereDirectQueue(
  host: InOutHost,
  requests: readonly QueueRequest[],
  ticksPerFrame: bigint,
  convention: OutPointConvention,
  opts: QueueRunOptions,
): Promise<QueueRunResult> {
  const result: QueueRunResult = { queued: 0, failed: 0, cancelled: 0, fatalErrors: [] };
  if (requests.length === 0) return result;

  let original: InOutPoints;
  try {
    original = await host.getInOut();
  } catch (e) {
    result.fatalErrors.push(`Could not read the sequence's current In/Out, nothing was queued: ${e}`);
    for (const r of requests) opts.onUpdate({ jobId: r.job.id, status: "failed", message: "Not started" });
    result.failed = requests.length;
    return result;
  }

  let touched = false;
  let stopReason: string | null = null;
  try {
    for (let i = 0; i < requests.length; i++) {
      const r = requests[i];
      if (stopReason === null && opts.isCancelled()) stopReason = "Cancelled by user";
      if (stopReason === null) {
        const changed = await opts.verifyUnchanged();
        if (changed) stopReason = `Timeline changed during queueing (${changed}). Rescan required.`;
      }
      if (stopReason !== null) {
        opts.onUpdate({ jobId: r.job.id, status: "cancelled", message: stopReason });
        result.cancelled++;
        continue;
      }

      opts.onUpdate({ jobId: r.job.id, status: "adding" });
      const want: InOutPoints = {
        inTicks: r.job.startTick,
        outTicks: hostOutPoint(parseTicks(r.job.endTick), ticksPerFrame, convention).toString(),
      };
      try {
        touched = true;
        const ok = await host.setInOut(want, `Track Range Exporter: range ${r.job.ordinal}`);
        if (!ok) throw new Error("Premiere refused to set In/Out");
        const got = await host.getInOut();
        if (!sameTicks(got.inTicks, want.inTicks) || !sameTicks(got.outTicks, want.outTicks)) {
          throw new Error(
            `In/Out read-back mismatch (wanted ${want.inTicks}-${want.outTicks}, ` +
              `got ${got.inTicks}-${got.outTicks})`,
          );
        }
        const queued = await host.queueInToOut(r.outputPath, r.presetPath);
        if (!queued) throw new Error("Premiere did not confirm the job was added to Media Encoder");
        opts.onUpdate({ jobId: r.job.id, status: "queued" });
        result.queued++;
      } catch (e) {
        opts.onUpdate({ jobId: r.job.id, status: "failed", message: String(e instanceof Error ? e.message : e) });
        result.failed++;
        // A failure while the sequence is being modified is not something to push through:
        // stop, restore, and let the user decide.
        stopReason = `Stopped after job ${r.job.ordinal} failed`;
      }
    }
  } finally {
    if (touched) {
      try {
        const ok = await host.setInOut(original, "Track Range Exporter: restore In/Out");
        const back = await host.getInOut();
        if (!ok || !sameTicks(back.inTicks, original.inTicks) || !sameTicks(back.outTicks, original.outTicks)) {
          result.fatalErrors.push(
            "Could not restore your original sequence In/Out points. Use Edit > Undo or set them " +
              `manually (original In ticks ${original.inTicks}, Out ticks ${original.outTicks}).`,
          );
        }
      } catch (e) {
        result.fatalErrors.push(
          `Restoring your original sequence In/Out failed: ${e}. Use Edit > Undo or set them manually.`,
        );
      }
    }
  }
  return result;
}
