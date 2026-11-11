/**
 * Thin, isolated wrapper around Premiere's UXP API (`require("premierepro")`, types from
 * @adobe/premierepro 27.0.0). Only this folder touches Premiere objects; everything it returns is
 * plain data so the domain layer stays testable.
 *
 * Read-only by design, except `PremiereInOutHost`, which is used only by the "Premiere direct"
 * queue route and always restores the user's in/out (see export/premiereEncoderAdapter.ts).
 */
import type {
  premierepro,
  Project,
  Sequence,
  TickTime,
  VideoClipTrackItem,
} from "@adobe/premierepro";
import type { GuideClipInfo, GuideTrackInfo, ScanSnapshot } from "../domain/types";
import type { InOutHost, InOutPoints } from "../export/premiereEncoderAdapter";

// eslint-disable-next-line @typescript-eslint/no-require-imports
declare const require: (id: string) => unknown;

let cached: premierepro | null = null;
export function ppro(): premierepro {
  if (!cached) cached = require("premierepro") as premierepro;
  return cached;
}

export class HostError extends Error {}

export interface ActiveContext {
  project: Project;
  sequence: Sequence;
}

export async function getActiveContext(): Promise<ActiveContext> {
  const project = await ppro().Project.getActiveProject();
  if (!project) throw new HostError("No project is open in Premiere Pro.");
  const sequence = await project.getActiveSequence();
  if (!sequence) throw new HostError("No active sequence. Open a sequence in the Timeline panel.");
  return { project, sequence };
}

export async function listVideoTracks(sequence: Sequence): Promise<GuideTrackInfo[]> {
  const count = await sequence.getVideoTrackCount();
  const tracks: GuideTrackInfo[] = [];
  for (let index = 0; index < count; index++) {
    const t = await sequence.getVideoTrack(index);
    let muted = false;
    try {
      muted = await t.isMuted();
    } catch {
      /* older builds: treat as unknown/visible */
    }
    tracks.push({ index, name: t.name ?? "", muted });
  }
  return tracks;
}

async function readFrameRate(sequence: Sequence): Promise<{ ticksPerFrame: string; dropFrame: boolean }> {
  const settings = await sequence.getSettings();
  const fr = settings.getVideoFrameRate();
  const tpf = fr.ticksPerFrame;
  if (!Number.isSafeInteger(tpf) || tpf <= 0) {
    throw new HostError(`Unexpected sequence frame duration (${tpf} ticks).`);
  }
  let dropFrame = false;
  try {
    const display = await settings.getVideoDisplayFormat();
    dropFrame = display.type === ppro().SequenceSettings.VIDEO_DISPLAY_FORMAT_2997;
  } catch {
    /* display format only affects timecode labels */
  }
  return { ticksPerFrame: String(tpf), dropFrame };
}

async function readItem(item: VideoClipTrackItem, hostIndex: number): Promise<GuideClipInfo> {
  const [start, end, name] = await Promise.all([item.getStartTime(), item.getEndTime(), item.getName()]);
  let disabled = false;
  let isAdjustmentLayer = false;
  try {
    disabled = await item.isDisabled();
  } catch {
    /* unknown -> treat as enabled */
  }
  try {
    isAdjustmentLayer = await item.isAdjustmentLayer();
  } catch {
    /* ignore */
  }
  return {
    hostIndex,
    name: name || `Clip ${hostIndex + 1}`,
    startTicks: start.ticks,
    endTicks: end.ticks,
    disabled,
    isAdjustmentLayer,
  };
}

/** Read everything needed to plan jobs. Does not modify the project. */
export async function scanTrack(ctx: ActiveContext, trackIndex: number): Promise<ScanSnapshot> {
  const { project, sequence } = ctx;
  const count = await sequence.getVideoTrackCount();
  if (trackIndex < 0 || trackIndex >= count) {
    throw new HostError(`Track V${trackIndex + 1} does not exist in this sequence.`);
  }
  const track = await sequence.getVideoTrack(trackIndex);
  const C = ppro().Constants;
  const items = await track.getTrackItems(C.TrackItemType.CLIP, false);
  const clips: GuideClipInfo[] = [];
  for (let i = 0; i < items.length; i++) clips.push(await readItem(items[i], i));

  const transitions: Array<{ startTicks: string; endTicks: string }> = [];
  try {
    const tItems = await track.getTrackItems(C.TrackItemType.TRANSITION, false);
    for (const t of tItems) {
      const [s, e] = await Promise.all([t.getStartTime(), t.getEndTime()]);
      transitions.push({ startTicks: s.ticks, endTicks: e.ticks });
    }
  } catch {
    /* transitions not readable on this build: no warnings rather than wrong ones */
  }

  let muted = false;
  try {
    muted = await track.isMuted();
  } catch {
    /* ignore */
  }
  let zeroPointTicks = "0";
  try {
    zeroPointTicks = (await sequence.getZeroPoint()).ticks;
  } catch {
    /* ignore */
  }
  const rate = await readFrameRate(sequence);
  return {
    projectPath: project.path ?? "",
    sequenceGuid: sequence.guid.toString(),
    sequenceName: sequence.name,
    ticksPerFrame: rate.ticksPerFrame,
    dropFrame: rate.dropFrame,
    zeroPointTicks,
    track: { index: trackIndex, name: track.name ?? "", muted },
    clips,
    transitionCount: transitions.length,
    transitions,
  };
}

/** Re-read the active context and verify it is still the scanned sequence. */
export async function getContextForSequence(sequenceGuid: string): Promise<ActiveContext> {
  const ctx = await getActiveContext();
  if (ctx.sequence.guid.toString() !== sequenceGuid) {
    throw new HostError("The active sequence changed since scanning. Rescan first.");
  }
  return ctx;
}

function tick(t: string): TickTime {
  return ppro().TickTime.createWithTicks(t);
}

/** In/out access for the "Premiere direct" route, using the documented lockedAccess + transaction. */
export class PremiereInOutHost implements InOutHost {
  constructor(private readonly ctx: ActiveContext) {}

  async getInOut(): Promise<InOutPoints> {
    const [i, o] = await Promise.all([this.ctx.sequence.getInPoint(), this.ctx.sequence.getOutPoint()]);
    return { inTicks: i.ticks, outTicks: o.ticks };
  }

  async setInOut(points: InOutPoints, undoLabel: string): Promise<boolean> {
    const { project, sequence } = this.ctx;
    let ok = false;
    project.lockedAccess(() => {
      ok = project.executeTransaction((compound) => {
        // Set out first when moving the range later, in first when moving earlier, so the
        // intermediate state never has in > out.
        const inA = sequence.createSetInPointAction(tick(points.inTicks));
        const outA = sequence.createSetOutPointAction(tick(points.outTicks));
        compound.addAction(outA);
        compound.addAction(inA);
      }, undoLabel);
    });
    if (!ok) return false;
    // If the host clamped one point against the old value of the other (e.g. new range lies
    // entirely before the old one), applying both once more lands on the requested range.
    const back = await this.getInOut();
    if (back.inTicks !== points.inTicks || back.outTicks !== points.outTicks) {
      project.lockedAccess(() => {
        ok = project.executeTransaction((compound) => {
          compound.addAction(sequence.createSetInPointAction(tick(points.inTicks)));
          compound.addAction(sequence.createSetOutPointAction(tick(points.outTicks)));
        }, undoLabel);
      });
    }
    return ok; // the caller verifies the final read-back and refuses to queue on mismatch
  }

  async queueInToOut(outputPath: string, presetPath: string): Promise<boolean> {
    const P = ppro();
    return P.EncoderManager.getManager().exportSequence(
      this.ctx.sequence,
      P.Constants.ExportType.QUEUE_TO_AME,
      outputPath,
      presetPath,
      false, // exportFull=false -> use the sequence In/Out we just set
    );
  }
}

export function isAmeInstalled(): boolean {
  try {
    return ppro().EncoderManager.getManager().isAMEInstalled;
  } catch {
    return false;
  }
}

export async function getPresetExtension(sequence: Sequence, presetPath: string): Promise<string> {
  return ppro().EncoderManager.getExportFileExtension(sequence, presetPath);
}

export async function launchEncoder(): Promise<boolean> {
  return ppro().EncoderManager.getManager().launchEncoder();
}

export async function startBatchEncode(): Promise<boolean> {
  return ppro().EncoderManager.getManager().startBatchEncode();
}

export async function saveProject(project: Project): Promise<boolean> {
  return project.save();
}
