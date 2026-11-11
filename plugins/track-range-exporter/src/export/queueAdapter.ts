import type { ExportJobPlan, JobStatus } from "../domain/types";

/** One fully-resolved request: queue this range of this sequence to this file with this preset. */
export interface QueueRequest {
  job: ExportJobPlan;
  outputPath: string;
  presetPath: string;
}

export interface JobUpdate {
  jobId: string;
  status: JobStatus;
  message?: string;
  /** Host-side job id when the host returns one (AME companion route). */
  hostJobId?: string;
}

export interface QueueRunResult {
  queued: number;
  failed: number;
  cancelled: number;
  /** Non-job problems, e.g. restoring the user's in/out failed. Shown prominently. */
  fatalErrors: string[];
}

export interface QueueRunOptions {
  onUpdate: (u: JobUpdate) => void;
  /** Returns true when the user pressed Cancel. Checked between jobs. */
  isCancelled: () => boolean;
  /**
   * Called before each job. Must resolve to null when the timeline is unchanged since scanning,
   * or to a human-readable reason when it changed (queueing then stops).
   */
  verifyUnchanged: () => Promise<string | null>;
}

/** How our exclusive [start, end) maps to the host's out point. Verified in host tests. */
export type OutPointConvention = "exclusive" | "inclusive";

export function hostOutPoint(
  endTicks: bigint,
  ticksPerFrame: bigint,
  convention: OutPointConvention,
): bigint {
  return convention === "exclusive" ? endTicks : endTicks - ticksPerFrame;
}
