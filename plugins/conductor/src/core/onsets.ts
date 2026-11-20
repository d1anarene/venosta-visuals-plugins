/**
 * Hit detection on a band envelope (e.g. the kick band). The onset function is how far the level
 * (dB) rises above the loudest point of the preceding `lookback` (excluding the last
 * `riseWindow`): beating between overlapping tones (kick tail + bass) makes notches and recoveries,
 * but never a rise above the recent maximum, so it does not count as a hit. A hit is a local
 * maximum of the onset function above an adaptive threshold
 * (local mean + `sensitivityDb`), with a level gate, a minimum gap and a re-arm hysteresis.
 * Each hit reports where the sound starts (steepest rise) and where it peaks (maximum level within
 * `peakSearch` after the onset): triggered events align their peak to `peakTime` by default.
 */
import { Envelope } from "./envelope";

export interface Hit {
  /** Seconds: steepest rise of the level. */
  onsetTime: number;
  /** Seconds: maximum level shortly after the onset (the "peak of the kick"). */
  peakTime: number;
  /** dBFS at the peak. */
  peakDb: number;
  /** dB rise of the onset (how sharp the hit is). */
  rise: number;
}

export interface OnsetOptions {
  /** dB above the local mean rise a peak must reach. Lower = more hits. */
  sensitivityDb: number;
  /** Ignore hits quieter than this many dB below the loudest point of the envelope. */
  rangeDb: number;
  /** Minimum time between hits (s); of two closer hits the stronger one wins. */
  minGap: number;
  /** The level must fall this many dB below the previous peak before another hit counts. */
  rearmDb: number;
  /** The rise is measured against the level from `lookback` until `riseWindow` before (s). */
  riseWindow: number;
  lookback: number;
  /** Window (s) after the onset in which the peak is searched. */
  peakSearch: number;
  /** Window (s) of the local mean for the adaptive threshold. */
  meanWindow: number;
}

export const DEFAULT_ONSETS: OnsetOptions = {
  sensitivityDb: 3,
  rangeDb: 30,
  minGap: 0.08,
  rearmDb: 3,
  riseWindow: 0.005,
  lookback: 0.06,
  peakSearch: 0.06,
  meanWindow: 0.5,
};

/** Rise of the level above the maximum of the preceding window [i - lookback, i - riseWindow]. */
export function onsetStrength(e: Envelope, riseWindow = DEFAULT_ONSETS.riseWindow, lookback = DEFAULT_ONSETS.lookback): Float32Array {
  const lag = Math.max(1, Math.round(riseWindow * e.rate));
  const back = Math.max(lag + 1, Math.round(lookback * e.rate));
  const n = e.db.length;
  const o = new Float32Array(n);
  // sliding-window maximum (monotonic deque) over db[i - back .. i - lag]
  const dq: number[] = [];
  let head = 0;
  for (let i = 0; i < n; ++i) {
    const add = i - lag;
    if (add >= 0) {
      while (dq.length > head && e.db[dq[dq.length - 1]] <= e.db[add]) dq.pop();
      dq.push(add);
    }
    while (dq.length > head && dq[head] < i - back) ++head;
    if (dq.length > head) o[i] = Math.max(0, e.db[i] - e.db[dq[head]]);
  }
  return o;
}

export function detectHits(e: Envelope, options: Partial<OnsetOptions> = {}): Hit[] {
  const opt = { ...DEFAULT_ONSETS, ...options };
  const n = e.db.length;
  if (n < 3) return [];
  const o = onsetStrength(e, opt.riseWindow, opt.lookback);
  let maxDb = -Infinity;
  for (const d of e.db) maxDb = Math.max(maxDb, d);
  const gate = maxDb - opt.rangeDb;
  // local mean of the onset function (prefix sums)
  const pre = new Float64Array(n + 1);
  for (let i = 0; i < n; ++i) pre[i + 1] = pre[i] + o[i];
  const half = Math.max(1, Math.round((opt.meanWindow * e.rate) / 2));
  const localPeak = Math.max(1, Math.round((opt.minGap * e.rate) / 2));
  const search = Math.max(1, Math.round(opt.peakSearch * e.rate));
  const candidates: Hit[] = [];
  for (let i = 1; i < n - 1; ++i) {
    if (o[i] <= 0) continue;
    const a = Math.max(0, i - half), b = Math.min(n, i + half);
    const mean = (pre[b] - pre[a]) / (b - a);
    if (o[i] < mean + opt.sensitivityDb) continue;
    let isMax = true;  // strict maximum to the left, >= to the right (one index per plateau)
    for (let j = Math.max(0, i - localPeak); j <= Math.min(n - 1, i + localPeak) && isMax; ++j)
      if (j < i ? o[j] >= o[i] : j > i && o[j] > o[i]) isMax = false;
    if (!isMax) continue;
    let p = i;
    for (let j = i; j < Math.min(n, i + search); ++j) if (e.db[j] > e.db[p]) p = j;
    if (e.db[p] < gate) continue;
    candidates.push({ onsetTime: i / e.rate, peakTime: p / e.rate, peakDb: e.db[p], rise: o[i] });
  }
  // minimum gap (stronger wins) and re-arm hysteresis, in time order
  const hits: Hit[] = [];
  for (const h of candidates) {
    const prev = hits[hits.length - 1];
    if (prev) {
      if (h.onsetTime - prev.onsetTime < opt.minGap) {
        if (h.rise > prev.rise) hits[hits.length - 1] = h;
        continue;
      }
      let lowest = Infinity;
      for (let j = Math.round(prev.peakTime * e.rate); j <= Math.round(h.onsetTime * e.rate); ++j) lowest = Math.min(lowest, e.db[j]);
      if (lowest > prev.peakDb - opt.rearmDb) continue;
    }
    hits.push(h);
  }
  return hits;
}
