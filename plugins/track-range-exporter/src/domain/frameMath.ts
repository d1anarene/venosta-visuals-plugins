/**
 * Exact, lossless time math for Premiere sequence time.
 *
 * Premiere represents time as integer "ticks" (exposed by the UXP API as a decimal string via
 * `TickTime.ticks`). We keep ticks as `bigint` internally and never use floating-point seconds as
 * the authoritative value. Frame duration comes from the sequence's own
 * `FrameRate.ticksPerFrame`; we never assume a universal ticks-per-frame constant.
 */

/** Premiere's tick resolution: 254,016,000,000 ticks per second. */
export const TICKS_PER_SECOND = 254016000000n;

/** Lossless tick value, serialised as a decimal string at API/JSON boundaries. */
export type TickString = string;

export function parseTicks(value: TickString | number | bigint): bigint {
  if (typeof value === "bigint") return value;
  if (typeof value === "number") {
    if (!Number.isSafeInteger(value)) {
      throw new RangeError(`Tick value ${value} is not a safe integer; use the string form`);
    }
    return BigInt(value);
  }
  const trimmed = value.trim();
  if (!/^-?\d+$/.test(trimmed)) throw new RangeError(`Invalid tick string: "${value}"`);
  return BigInt(trimmed);
}

export function ticksToString(ticks: bigint): TickString {
  return ticks.toString(10);
}

/**
 * A sequence frame rate, defined by integer ticks per frame (as reported by Premiere), plus the
 * nominal timecode base used for display (24, 25, 30, 50, 60...).
 */
export interface FrameRateInfo {
  ticksPerFrame: bigint;
  /** True for 29.97/59.94 drop-frame timecode display. Does not affect range math. */
  dropFrame: boolean;
}

/** Common Premiere rates, ticksPerFrame = 254016000000 / fps (exact integers for all of these). */
export const KNOWN_RATES: Record<string, bigint> = {
  "23.976": 10594584000n, // 254016000000 * 1001 / 24000
  "24": 10584000000n,
  "25": 10160640000n,
  "29.97": 8475667200n, // * 1001 / 30000
  "30": 8467200000n,
  "50": 5080320000n,
  "59.94": 4237833600n, // * 1001 / 60000
  "60": 4233600000n,
};

/** Frames per second as an exact rational: TICKS_PER_SECOND / ticksPerFrame. */
export function fpsRational(rate: FrameRateInfo): { num: bigint; den: bigint } {
  return { num: TICKS_PER_SECOND, den: rate.ticksPerFrame };
}

/** Nominal integer timecode base (e.g. 30 for 29.97, 24 for 23.976). */
export function timecodeBase(rate: FrameRateInfo): number {
  // round(TICKS_PER_SECOND / ticksPerFrame)
  const q = (TICKS_PER_SECOND * 2n + rate.ticksPerFrame) / (rate.ticksPerFrame * 2n);
  return Number(q);
}

export function isFrameAligned(ticks: bigint, rate: FrameRateInfo): boolean {
  return ticks % rate.ticksPerFrame === 0n;
}

/** Floor division that also works for negative values. */
function floorDiv(a: bigint, b: bigint): bigint {
  const q = a / b;
  return a % b !== 0n && (a < 0n) !== (b < 0n) ? q - 1n : q;
}

/** Index of the frame that contains `ticks` (floor). */
export function frameIndexAt(ticks: bigint, rate: FrameRateInfo): bigint {
  return floorDiv(ticks, rate.ticksPerFrame);
}

/**
 * Number of whole frames in the half-open interval [start, end).
 * Exact when both ends are frame aligned; otherwise counts frames whose start lies in range.
 */
export function frameCount(start: bigint, end: bigint, rate: FrameRateInfo): bigint {
  if (end <= start) return 0n;
  const firstFrame = -floorDiv(-start, rate.ticksPerFrame); // ceil(start / tpf)
  const lastExclusive = -floorDiv(-end, rate.ticksPerFrame); // ceil(end / tpf)
  return lastExclusive - firstFrame;
}

export function framesToTicks(frames: bigint, rate: FrameRateInfo): bigint {
  return frames * rate.ticksPerFrame;
}

/**
 * Format a frame index as SMPTE timecode. Drop-frame (`;` separator) is applied for
 * 29.97/59.94 when `rate.dropFrame` is true. Negative frames are prefixed with "-".
 */
export function formatTimecode(frameIndex: bigint, rate: FrameRateInfo): string {
  const negative = frameIndex < 0n;
  let frames = negative ? -frameIndex : frameIndex;
  const base = BigInt(timecodeBase(rate));
  const useDrop = rate.dropFrame && (base === 30n || base === 60n);
  if (useDrop) {
    // SMPTE drop-frame: skip 2 (or 4 at 60) frame numbers every minute except every 10th.
    const dropPerMinute = base === 30n ? 2n : 4n;
    const framesPer10Min = base * 600n - dropPerMinute * 9n;
    const framesPerMinute = base * 60n - dropPerMinute;
    const tenMinBlocks = frames / framesPer10Min;
    const rem = frames % framesPer10Min;
    let extra = 0n;
    if (rem > dropPerMinute) {
      extra = dropPerMinute * ((rem - dropPerMinute) / framesPerMinute);
    }
    frames = frames + dropPerMinute * 9n * tenMinBlocks + extra;
  }
  const ff = frames % base;
  const totalSeconds = frames / base;
  const ss = totalSeconds % 60n;
  const mm = (totalSeconds / 60n) % 60n;
  const hh = totalSeconds / 3600n;
  const pad = (n: bigint) => n.toString().padStart(2, "0");
  const sep = useDrop ? ";" : ":";
  return `${negative ? "-" : ""}${pad(hh)}:${pad(mm)}:${pad(ss)}${sep}${pad(ff)}`;
}

/** Format a tick position (relative to the sequence's zero point offset) as timecode. */
export function ticksToTimecode(ticks: bigint, rate: FrameRateInfo, zeroPoint = 0n): string {
  return formatTimecode(frameIndexAt(ticks + zeroPoint, rate), rate);
}

/** Display-only seconds (never authoritative). */
export function ticksToSecondsApprox(ticks: bigint): number {
  return Number(ticks) / Number(TICKS_PER_SECOND);
}
