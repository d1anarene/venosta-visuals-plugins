/**
 * From a value function to keyframes: one per frame at exact frame times (Premiere ticks), and an
 * optional simplification (Ramer-Douglas-Peucker on the linear interpolation between keys) that
 * keeps every value within `tolerance` of the per-frame curve. Pure, deterministic.
 */
export const TICKS_PER_SECOND = 254016000000;

export interface Key {
  /** Premiere ticks (integer, as a number; < 2^53 for > 9 hours). */
  ticks: number;
  value: number;
}

/** One key per frame from startTicks (inclusive) to endTicks (exclusive). */
export function sampleFrames(f: (seconds: number) => number, startTicks: number, endTicks: number, ticksPerFrame: number): Key[] {
  const keys: Key[] = [];
  for (let t = startTicks; t < endTicks; t += ticksPerFrame) keys.push({ ticks: t, value: f(t / TICKS_PER_SECOND) });
  return keys;
}

function deviation(keys: Key[], a: number, b: number): { index: number; err: number } {
  const ka = keys[a], kb = keys[b];
  let worst = -1, err = 0;
  for (let i = a + 1; i < b; ++i) {
    const u = (keys[i].ticks - ka.ticks) / (kb.ticks - ka.ticks);
    const d = Math.abs(keys[i].value - (ka.value + (kb.value - ka.value) * u));
    if (d > err) { err = d; worst = i; }
  }
  return { index: worst, err };
}

/** Fewest keys (greedy RDP) with linear interpolation within `tolerance` of every input key. */
export function simplify(keys: Key[], tolerance: number): Key[] {
  if (keys.length <= 2 || !(tolerance > 0)) return keys.slice();
  const keep = new Uint8Array(keys.length);
  keep[0] = keep[keys.length - 1] = 1;
  const stack: [number, number][] = [[0, keys.length - 1]];
  while (stack.length) {
    const [a, b] = stack.pop()!;
    const { index, err } = deviation(keys, a, b);
    if (index >= 0 && err > tolerance) {
      keep[index] = 1;
      stack.push([a, index], [index, b]);
    }
  }
  return keys.filter((_, i) => keep[i]);
}

/** Largest difference between the linear interpolation of `reduced` and `full` at full's times. */
export function maxError(full: Key[], reduced: Key[]): number {
  let j = 0, e = 0;
  for (const k of full) {
    while (j < reduced.length - 2 && reduced[j + 1].ticks <= k.ticks) ++j;
    const a = reduced[j], b = reduced[Math.min(j + 1, reduced.length - 1)];
    const u = b.ticks === a.ticks ? 0 : (k.ticks - a.ticks) / (b.ticks - a.ticks);
    e = Math.max(e, Math.abs(k.value - (a.value + (b.value - a.value) * Math.min(1, Math.max(0, u)))));
  }
  return e;
}
