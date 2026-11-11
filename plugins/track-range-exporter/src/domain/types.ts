import type { TickString } from "./frameMath";

/** Raw, host-independent description of one clip on the guide track (sequence coordinates). */
export interface GuideClipInfo {
  /** Position of the item in the track's item list as returned by the host. */
  hostIndex: number;
  name: string;
  /** Start in sequence ticks (inclusive). */
  startTicks: TickString;
  /** End in sequence ticks (exclusive). */
  endTicks: TickString;
  disabled: boolean;
  isAdjustmentLayer: boolean;
  /** True if the host reports transitions touching this clip (when detectable). */
  hasTransition?: boolean;
}

export interface GuideTrackInfo {
  /** Zero-based API index. UI label "V1" == index 0. */
  index: number;
  /** Premiere's custom track name, if any. */
  name: string;
  muted: boolean;
}

export interface ScanSnapshot {
  projectPath: string;
  sequenceGuid: string;
  sequenceName: string;
  ticksPerFrame: TickString;
  dropFrame: boolean;
  zeroPointTicks: TickString;
  track: GuideTrackInfo;
  clips: GuideClipInfo[];
  /** Number of transition items on the guide track (for ambiguity warnings). */
  transitionCount: number;
  /** Transition intervals on the guide track, if readable. */
  transitions: Array<{ startTicks: TickString; endTicks: TickString }>;
}

export type JobStatus =
  | "scanned"
  | "ready"
  | "adding"
  | "queued"
  | "rendering"
  | "done"
  | "failed"
  | "cancelled"
  | "skipped"
  | "unsupported";

export type WarningCode =
  | "DISABLED_CLIP"
  | "ZERO_DURATION"
  | "NOT_FRAME_ALIGNED"
  | "TRANSITION_AT_BOUNDARY"
  | "OVERLAPS_PREVIOUS"
  | "ADJUSTMENT_LAYER_AS_GUIDE"
  | "MUTED_GUIDE_TRACK";

export interface PlanWarning {
  code: WarningCode;
  message: string;
}

/** Immutable plan for one export job. Pure data: no host object handles. */
export interface ExportJobPlan {
  readonly id: string;
  readonly ordinal: number; // 1-based among all scanned clips
  readonly sequenceGuid: string;
  readonly guideTrackIndex: number;
  readonly guideClipHostIndex: number;
  readonly guideClipName: string;
  readonly startTick: TickString; // inclusive
  readonly endTick: TickString; // exclusive
  readonly expectedFrames: number;
  readonly defaultBaseName: string; // without extension
  readonly includeByDefault: boolean;
  readonly skippable: boolean; // true when the job can never be queued (e.g. zero length)
  readonly warnings: readonly PlanWarning[];
}

export interface PlanOptions {
  includeDisabledClips: boolean;
}

export interface ExportPlan {
  readonly snapshot: ScanSnapshot;
  readonly jobs: readonly ExportJobPlan[];
  readonly trackWarnings: readonly PlanWarning[];
  /** Fingerprint of the scan used to detect stale plans. */
  readonly fingerprint: string;
}
