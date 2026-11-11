import {
  frameCount,
  isFrameAligned,
  parseTicks,
  type FrameRateInfo,
} from "./frameMath";
import { cleanBaseName } from "./naming";
import type {
  ExportJobPlan,
  ExportPlan,
  GuideClipInfo,
  PlanOptions,
  PlanWarning,
  ScanSnapshot,
} from "./types";

export const DEFAULT_PLAN_OPTIONS: PlanOptions = { includeDisabledClips: false };

/** UI label for a zero-based video track index: 0 -> "V1". */
export function videoTrackLabel(index: number): string {
  if (!Number.isInteger(index) || index < 0) throw new RangeError(`Bad track index ${index}`);
  return `V${index + 1}`;
}

export function rateOf(snapshot: ScanSnapshot): FrameRateInfo {
  return { ticksPerFrame: parseTicks(snapshot.ticksPerFrame), dropFrame: snapshot.dropFrame };
}

/**
 * Deterministic fingerprint of everything that defines the ranges. If the timeline changes after
 * scanning, a re-read produces a different fingerprint and queueing is refused until rescan.
 */
export function fingerprintSnapshot(s: ScanSnapshot): string {
  const parts = [
    s.projectPath,
    s.sequenceGuid,
    s.ticksPerFrame,
    String(s.track.index),
    ...s.clips.map(
      (c) => `${c.hostIndex}|${c.name}|${c.startTicks}|${c.endTicks}|${c.disabled ? 1 : 0}`,
    ),
  ];
  // FNV-1a 64-bit over the joined string; collisions are irrelevant at this scale.
  let h = 0xcbf29ce484222325n;
  const text = parts.join("\n");
  for (let i = 0; i < text.length; i++) {
    h ^= BigInt(text.charCodeAt(i));
    h = (h * 0x100000001b3n) & 0xffffffffffffffffn;
  }
  return h.toString(16).padStart(16, "0");
}

/**
 * Pure transformation: guide-track clips -> one export job per clip, sorted by timeline start.
 * Clips are never merged (even adjacent clips of the same source). Gaps produce no jobs.
 */
export function buildPlan(
  snapshot: ScanSnapshot,
  options: PlanOptions = DEFAULT_PLAN_OPTIONS,
): ExportPlan {
  const rate = rateOf(snapshot);
  if (rate.ticksPerFrame <= 0n) throw new RangeError("Sequence frame rate is invalid");

  const sorted: GuideClipInfo[] = [...snapshot.clips].sort((a, b) => {
    const d = parseTicks(a.startTicks) - parseTicks(b.startTicks);
    if (d !== 0n) return d < 0n ? -1 : 1;
    return a.hostIndex - b.hostIndex; // stable tie-break
  });

  const transitions = snapshot.transitions.map((t) => ({
    start: parseTicks(t.startTicks),
    end: parseTicks(t.endTicks),
  }));

  const trackWarnings: PlanWarning[] = [];
  if (snapshot.track.muted) {
    trackWarnings.push({
      code: "MUTED_GUIDE_TRACK",
      message:
        `${videoTrackLabel(snapshot.track.index)} is hidden (track output off). Its clips are ` +
        "still used as range guides, but they will not be visible in the export.",
    });
  }

  let prevEnd: bigint | null = null;
  const jobs: ExportJobPlan[] = sorted.map((clip, i) => {
    const start = parseTicks(clip.startTicks);
    const end = parseTicks(clip.endTicks);
    const warnings: PlanWarning[] = [];
    let skippable = false;

    if (end <= start) {
      skippable = true;
      warnings.push({ code: "ZERO_DURATION", message: "Clip has no duration; it is skipped." });
    }
    if (!isFrameAligned(start, rate) || !isFrameAligned(end, rate)) {
      warnings.push({
        code: "NOT_FRAME_ALIGNED",
        message:
          "Clip boundary is not on an exact sequence frame boundary. The range is sent as-is " +
          "(not shifted); Premiere decides which partial frame is rendered.",
      });
    }
    if (clip.disabled) {
      warnings.push({
        code: "DISABLED_CLIP",
        message: options.includeDisabledClips
          ? "Clip is disabled in Premiere; included because 'include disabled clips' is on."
          : "Clip is disabled in Premiere; excluded by default.",
      });
    }
    if (clip.isAdjustmentLayer) {
      warnings.push({
        code: "ADJUSTMENT_LAYER_AS_GUIDE",
        message: "This guide item is an adjustment layer. Is this the right guide track?",
      });
    }
    if (prevEnd !== null && start < prevEnd) {
      warnings.push({
        code: "OVERLAPS_PREVIOUS",
        message: "Range overlaps the previous guide clip (unexpected on a single track).",
      });
    }
    const touchesTransition =
      clip.hasTransition === true ||
      transitions.some((t) => t.start < end && t.end > start);
    if (touchesTransition) {
      warnings.push({
        code: "TRANSITION_AT_BOUNDARY",
        message:
          "A transition overlaps this clip. The export covers the clip's own timeline range " +
          "exactly; part of the transition (and the neighbouring shot inside it) may be visible.",
      });
    }
    prevEnd = prevEnd === null || end > prevEnd ? end : prevEnd;

    const frames = frameCount(start, end, rate);
    const ordinal = i + 1;
    const includeByDefault = !skippable && (!clip.disabled || options.includeDisabledClips);
    return Object.freeze({
      id: `${snapshot.sequenceGuid}:${snapshot.track.index}:${clip.hostIndex}:${clip.startTicks}`,
      ordinal,
      sequenceGuid: snapshot.sequenceGuid,
      guideTrackIndex: snapshot.track.index,
      guideClipHostIndex: clip.hostIndex,
      guideClipName: clip.name,
      startTick: clip.startTicks,
      endTick: clip.endTicks,
      expectedFrames: Number(frames),
      defaultBaseName: `${String(ordinal).padStart(3, "0")}_${cleanBaseName(clip.name)}`,
      includeByDefault,
      skippable,
      warnings: Object.freeze(warnings),
    });
  });

  return Object.freeze({
    snapshot,
    jobs: Object.freeze(jobs),
    trackWarnings: Object.freeze(trackWarnings),
    fingerprint: fingerprintSnapshot(snapshot),
  });
}

export type StaleReason =
  | "PROJECT_CHANGED"
  | "SEQUENCE_CHANGED"
  | "TRACK_CHANGED"
  | "CLIPS_CHANGED"
  | "FRAME_RATE_CHANGED";

/** Compare a stored plan to a fresh read of the timeline. Returns null if still valid. */
export function staleReason(plan: ExportPlan, fresh: ScanSnapshot): StaleReason | null {
  const s = plan.snapshot;
  if (s.projectPath !== fresh.projectPath) return "PROJECT_CHANGED";
  if (s.sequenceGuid !== fresh.sequenceGuid) return "SEQUENCE_CHANGED";
  if (s.track.index !== fresh.track.index) return "TRACK_CHANGED";
  if (s.ticksPerFrame !== fresh.ticksPerFrame) return "FRAME_RATE_CHANGED";
  if (plan.fingerprint !== fingerprintSnapshot(fresh)) return "CLIPS_CHANGED";
  return null;
}

/** Sum of frames for the selected jobs. */
export function totalFrames(jobs: readonly ExportJobPlan[]): number {
  return jobs.reduce((n, j) => n + j.expectedFrames, 0);
}
