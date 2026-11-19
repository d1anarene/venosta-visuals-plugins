/** Test values for the keyframe spike: a pulse train (one pulse every `period` keys, quick rise,
 *  quadratic decay), base + amplitude * shape. Pure, unit-tested. */
export function pulseValues(count: number, base: number, amplitude: number, period = 12): number[] {
  const out: number[] = [];
  for (let i = 0; i < count; ++i) {
    const p = i % period;
    const shape = p === 0 ? 1 : Math.max(0, 1 - p / (period * 0.6)) ** 2;
    out.push(base + amplitude * shape);
  }
  return out;
}
