/**
 * Time-dependent outputs (AGENTS.md §4): a speed curve over timeline time integrates to source
 * time, so every ramp moves everything after it. Evaluated sequentially, frame by frame.
 *  - "sequential": ramps as given; reports the accumulated drift (source seconds gained).
 *  - "compensated": between consecutive anchors (hits / beats) the excess of each section is
 *    removed by a uniform slow-down over the rest of that section, so source and timeline time
 *    meet again at every anchor (zero drift at anchors); reports the lowest speed used.
 * Values are speeds as factors (1 = 100 %). Pure, deterministic.
 */
export interface TimeMap {
  /** Timeline seconds of each step (frame). */
  t: number[];
  /** Speed factor used over [t[i], t[i+1]). */
  speed: number[];
  /** Source seconds (from the clip's first source frame) at t[i]. */
  source: number[];
  /** source(end) - (end - start): positive = more source used than at 100 %. */
  drift: number;
  minSpeed: number;
}

export function integrate(t: number[], speed: number[]): number[] {
  const src = [0];
  for (let i = 1; i < t.length; ++i) src.push(src[i - 1] + speed[i - 1] * (t[i] - t[i - 1]));
  return src;
}

function finish(t: number[], speed: number[]): TimeMap {
  const source = integrate(t, speed);
  const span = t.length ? t[t.length - 1] - t[0] : 0;
  return { t, speed, source, drift: (source[source.length - 1] ?? 0) - span, minSpeed: Math.min(...speed) };
}

/** Speed sampled at frames from start (s) for `frames` steps of `frameSec`. */
export function speedMap(f: (t: number) => number, start: number, frames: number, frameSec: number,
                         policy: "sequential" | "compensated" = "sequential", anchors: number[] = []): TimeMap {
  const t: number[] = [];
  const speed: number[] = [];
  for (let i = 0; i <= frames; ++i) {
    t.push(start + i * frameSec);
    speed.push(f(start + i * frameSec));
  }
  if (policy === "sequential") return finish(t, speed);
  // section boundaries in frame indices: start, every anchor inside, end
  const cuts = [0];
  for (const a of [...anchors].sort((x, y) => x - y)) {
    const i = Math.round((a - start) / frameSec);
    if (i > cuts[cuts.length - 1] && i < frames) cuts.push(i);
  }
  cuts.push(frames);
  for (let s = 0; s + 1 < cuts.length; ++s) {
    const a = cuts[s], b = cuts[s + 1];
    let excess = 0;
    for (let i = a; i < b; ++i) excess += (speed[i] - 1) * frameSec;
    // remove the excess uniformly over the frames of the section that are not part of a ramp
    const free: number[] = [];
    for (let i = a; i < b; ++i) if (Math.abs(speed[i] - 1) < 1e-9) free.push(i);
    const over = free.length ? free : Array.from({ length: b - a }, (_, k) => a + k);
    const d = excess / (over.length * frameSec);
    for (const i of over) speed[i] -= d;
  }
  return finish(t, speed);
}
