import { describe, expect, it } from "vitest";
import {
  KNOWN_RATES, TICKS_PER_SECOND, formatTimecode, frameCount, frameIndexAt, isFrameAligned,
  parseTicks, timecodeBase, ticksToTimecode,
} from "../src/domain/frameMath";

const rate = (r: string, dropFrame = false) => ({ ticksPerFrame: KNOWN_RATES[r], dropFrame });

describe("frame math", () => {
  it("known rates are exact integer ticks per frame", () => {
    const fps: Record<string, [bigint, bigint]> = {
      "23.976": [24000n, 1001n], "24": [24n, 1n], "25": [25n, 1n], "29.97": [30000n, 1001n],
      "30": [30n, 1n], "50": [50n, 1n], "59.94": [60000n, 1001n], "60": [60n, 1n],
    };
    for (const [k, [num, den]] of Object.entries(fps)) {
      expect(TICKS_PER_SECOND * den % num).toBe(0n);
      expect(KNOWN_RATES[k]).toBe((TICKS_PER_SECOND * den) / num);
    }
  });

  it.each(Object.keys(KNOWN_RATES))("frame counts are exact at %s fps (no off-by-one)", (r) => {
    const R = rate(r);
    for (const [s, e] of [[0, 1], [0, 50], [7, 12], [1000, 1001], [86399, 172800]]) {
      const start = BigInt(s) * R.ticksPerFrame;
      const end = BigInt(e) * R.ticksPerFrame;
      expect(frameCount(start, end, R)).toBe(BigInt(e - s));
      expect(frameIndexAt(start, R)).toBe(BigInt(s));
      expect(frameIndexAt(end - 1n, R)).toBe(BigInt(e - 1)); // last frame of [s,e)
      expect(isFrameAligned(start, R)).toBe(true);
    }
  });

  it("adjacent ranges share no frame and leave no gap", () => {
    const R = rate("23.976");
    const a = [0n, 48n * R.ticksPerFrame];
    const b = [a[1], a[1] + 168n * R.ticksPerFrame];
    expect(frameIndexAt(a[1] - 1n, R) + 1n).toBe(frameIndexAt(b[0], R));
    expect(frameCount(a[0], a[1], R) + frameCount(b[0], b[1], R)).toBe(frameCount(a[0], b[1], R));
  });

  it("does not trust floating point seconds", () => {
    // 1 hour at 29.97 = 107892 frames + fraction; ticks stay exact
    const R = rate("29.97");
    const oneHour = 3600n * TICKS_PER_SECOND;
    expect(frameCount(0n, oneHour, R)).toBe(107893n); // ceil(107892.107...)
    expect(isFrameAligned(oneHour, R)).toBe(false);
  });

  it("parses ticks strictly", () => {
    expect(parseTicks("254016000000")).toBe(254016000000n);
    expect(() => parseTicks("1.5")).toThrow();
    expect(() => parseTicks(2 ** 60)).toThrow();
  });

  it("timecode bases", () => {
    expect(timecodeBase(rate("23.976"))).toBe(24);
    expect(timecodeBase(rate("29.97"))).toBe(30);
    expect(timecodeBase(rate("59.94"))).toBe(60);
    expect(timecodeBase(rate("25"))).toBe(25);
  });

  it("formats non-drop timecode", () => {
    expect(formatTimecode(0n, rate("25"))).toBe("00:00:00:00");
    expect(formatTimecode(125n, rate("25"))).toBe("00:00:05:00");
    expect(formatTimecode(24n * 3600n, rate("23.976"))).toBe("01:00:00:00");
    expect(ticksToTimecode(8n * TICKS_PER_SECOND, rate("25"))).toBe("00:00:08:00");
  });

  it("formats SMPTE drop-frame timecode at 29.97", () => {
    const R = rate("29.97", true);
    expect(formatTimecode(1799n, R)).toBe("00:00:59;29");
    expect(formatTimecode(1800n, R)).toBe("00:01:00;02"); // ;00 and ;01 dropped
    expect(formatTimecode(17982n, R)).toBe("00:10:00;00"); // 10th minute not dropped
    expect(formatTimecode(107892n, R)).toBe("01:00:00;00");
  });

  it("formats drop-frame at 59.94", () => {
    const R = rate("59.94", true);
    expect(formatTimecode(3600n, R)).toBe("00:01:00;04");
    expect(formatTimecode(215784n, R)).toBe("01:00:00;00");
  });
});
