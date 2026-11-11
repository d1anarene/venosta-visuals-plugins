import { KNOWN_RATES } from "../src/domain/frameMath";
import type { GuideClipInfo, ScanSnapshot } from "../src/domain/types";

export function clip(
  hostIndex: number,
  name: string,
  startFrame: number,
  endFrame: number,
  rate = "25",
  extra: Partial<GuideClipInfo> = {},
): GuideClipInfo {
  const tpf = KNOWN_RATES[rate];
  return {
    hostIndex,
    name,
    startTicks: (BigInt(startFrame) * tpf).toString(),
    endTicks: (BigInt(endFrame) * tpf).toString(),
    disabled: false,
    isAdjustmentLayer: false,
    ...extra,
  };
}

export function snapshot(clips: GuideClipInfo[], rate = "25", extra: Partial<ScanSnapshot> = {}): ScanSnapshot {
  return {
    projectPath: "/Projects/test.prproj",
    sequenceGuid: "seq-guid-1",
    sequenceName: "Graded",
    ticksPerFrame: KNOWN_RATES[rate].toString(),
    dropFrame: false,
    zeroPointTicks: "0",
    track: { index: 0, name: "Main Footage", muted: false },
    clips,
    transitionCount: 0,
    transitions: [],
    ...extra,
  };
}
