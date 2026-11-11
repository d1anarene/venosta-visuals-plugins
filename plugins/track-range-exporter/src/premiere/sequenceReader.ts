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
import { diag } from "../ui/diagnostics";

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

export interface SequenceRef {
  guid: string;
  name: string;
  active: boolean;
}

let lastProject: Project | null = null;

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/**
 * The active project. Premiere 27 sometimes reports no active project for a moment (e.g. while
 * this panel has focus or was just docked), so retry briefly and fall back to the last project we
 * saw, as long as Premiere still knows it (looked up by GUID).
 */
async function activeProject(): Promise<Project> {
  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      const project = await ppro().Project.getActiveProject();
      if (project) {
        lastProject = project;
        return project;
      }
    } catch (e) {
      diag("debug", `getActiveProject attempt ${attempt + 1} threw`, e);
    }
    await sleep(150);
  }
  if (lastProject) {
    try {
      const again = ppro().Project.getProject(lastProject.guid);
      if (again) {
        diag("debug", "No active project reported; using the last known project");
        return again;
      }
    } catch (e) {
      diag("debug", "Last known project lookup failed", e);
    }
  }
  diag("warn", "Premiere reports no open project");
  throw new HostError("Premiere reports no open project. Click into the Timeline, then press ↻.");
}

/** All sequences of the active project, with the active one marked. */
export async function listSequences(): Promise<SequenceRef[]> {
  const project = await activeProject();
  const seqs = (await project.getSequences()) ?? [];
  let activeGuid = "";
  try {
    const active = await project.getActiveSequence();
    if (active) activeGuid = active.guid.toString();
  } catch {
    /* no active sequence */
  }
  return Array.from(seqs).map((sq) => ({ guid: sq.guid.toString(), name: sq.name, active: sq.guid.toString() === activeGuid }));
}

/** Context for a specific sequence (by GUID), or the active sequence when no GUID is given. */
export async function getContext(sequenceGuid?: string | null): Promise<ActiveContext> {
  const project = await activeProject();
  if (sequenceGuid) {
    const seqs = (await project.getSequences()) ?? [];
    const found = Array.from(seqs).find((sq) => sq.guid.toString() === sequenceGuid);
    if (!found) throw new HostError("The chosen sequence no longer exists in this project. Pick it again.");
    return { project, sequence: found };
  }
  const sequence = await project.getActiveSequence();
  if (!sequence) throw new HostError("No sequence chosen. Pick one in the Sequence list.");
  return { project, sequence };
}

export async function getActiveContext(): Promise<ActiveContext> {
  return getContext(null);
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

const TICKS_PER_SECOND = 254016000000;

function positiveIntTicks(v: unknown): number | null {
  const n = typeof v === "string" ? Number(v.trim()) : typeof v === "number" ? v : NaN;
  return Number.isSafeInteger(n) && n > 0 ? n : null;
}

/**
 * Ticks per frame of the sequence. Tries, in order: Sequence.getTimebase() (ticks per frame as a
 * string), FrameRate.ticksPerFrame, and frames per second (FrameRate.value) converted exactly for
 * the standard rates. The first valid integer wins.
 */
async function readFrameRate(sequence: Sequence): Promise<{ ticksPerFrame: string; dropFrame: boolean }> {
  const tried: string[] = [];
  let tpf: number | null = null;
  try {
    const tb = await sequence.getTimebase();
    tpf = positiveIntTicks(tb);
    tried.push(`timebase=${tb}`);
  } catch (e) {
    tried.push(`timebase error ${e}`);
  }
  let settings: Awaited<ReturnType<Sequence["getSettings"]>> | null = null;
  try {
    settings = await sequence.getSettings();
  } catch (e) {
    tried.push(`settings error ${e}`);
  }
  if (tpf === null && settings) {
    try {
      const fr = await settings.getVideoFrameRate();
      tpf = positiveIntTicks(fr.ticksPerFrame);
      tried.push(`ticksPerFrame=${fr.ticksPerFrame}`);
      if (tpf === null && typeof fr.value === "number" && fr.value > 0) {
        // Standard rates: 1000/1001 family when within 0.01 of an NTSC rate, else exact.
        const fps = fr.value;
        const ntsc = [23.976, 29.97, 47.952, 59.94, 119.88].find((r) => Math.abs(r - fps) < 0.01);
        // NTSC rates are N*1000/1001 fps (e.g. 24000/1001); others are exact.
        const t = ntsc ? (TICKS_PER_SECOND * 1001) / (Math.round(ntsc * 1.001) * 1000) : TICKS_PER_SECOND / fps;
        tpf = positiveIntTicks(Math.round(t));
        tried.push(`fps=${fps}`);
      }
    } catch (e) {
      tried.push(`frame rate error ${e}`);
    }
  }
  diag("debug", `Frame rate: ${tried.join(", ")} -> ticksPerFrame ${tpf}`);
  if (tpf === null) throw new HostError(`Could not read the sequence frame rate (${tried.join(", ")}).`);
  let dropFrame = false;
  try {
    if (settings) {
      const display = await settings.getVideoDisplayFormat();
      dropFrame = display.type === ppro().SequenceSettings.VIDEO_DISPLAY_FORMAT_2997;
    }
  } catch {
    /* display format only affects timecode labels */
  }
  return { ticksPerFrame: String(tpf), dropFrame };
}

function ticksOf(t: TickTime, what: string): string {
  const s = (t as { ticks?: unknown }).ticks;
  if (typeof s === "string" && /^-?\d+$/.test(s)) return s;
  const n = (t as { ticksNumber?: unknown }).ticksNumber;
  if (typeof n === "number" && Number.isSafeInteger(n)) return String(n);
  throw new HostError(`Premiere returned an unreadable time for ${what}.`);
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
    startTicks: ticksOf(start, "a clip start"),
    endTicks: ticksOf(end, "a clip end"),
    disabled,
    isAdjustmentLayer,
  };
}

/** Read everything needed to plan jobs. Does not modify the project. */
async function step<T>(label: string, fn: () => Promise<T> | T): Promise<T> {
  try {
    return await fn();
  } catch (e) {
    diag("error", `Step "${label}" failed`, e);
    if (e instanceof HostError) throw e;
    throw new HostError(`${label} failed: ${e instanceof Error ? e.message : String(e)}`);
  }
}

/** Read everything needed to plan jobs. Does not modify the project. */
export async function scanTrack(ctx: ActiveContext, trackIndex: number): Promise<ScanSnapshot> {
  const { project, sequence } = ctx;
  const count = await step("Reading the track count", () => sequence.getVideoTrackCount());
  if (trackIndex < 0 || trackIndex >= count) {
    throw new HostError(`Track V${trackIndex + 1} does not exist in this sequence.`);
  }
  const track = await step("Opening the guide track", () => sequence.getVideoTrack(trackIndex));
  const C = ppro().Constants;
  const items = Array.from(
    (await step("Listing clips on the track", () => track.getTrackItems(C.TrackItemType.CLIP, false))) ?? [],
  );
  diag("debug", `Sequence "${sequence.name}": ${count} video track(s); V${trackIndex + 1} has ${items.length} clip item(s)`);
  const clips: GuideClipInfo[] = [];
  for (let i = 0; i < items.length; i++) {
    clips.push(await step(`Reading clip ${i + 1}`, () => readItem(items[i], i)));
  }
  if (clips.length) {
    const c = clips[0];
    diag("debug", `First clip "${c.name}": start ${c.startTicks}, end ${c.endTicks}, disabled ${c.disabled}`);
  }

  const transitions: Array<{ startTicks: string; endTicks: string }> = [];
  try {
    const tItems = Array.from((await track.getTrackItems(C.TrackItemType.TRANSITION, false)) ?? []);
    for (const t of tItems) {
      const [s0, e0] = await Promise.all([t.getStartTime(), t.getEndTime()]);
      transitions.push({ startTicks: ticksOf(s0, "a transition"), endTicks: ticksOf(e0, "a transition") });
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
    zeroPointTicks = ticksOf(await sequence.getZeroPoint(), "the sequence start");
  } catch {
    /* timecode labels then start at 00:00:00:00 */
  }
  const rate = await step("Reading the frame rate", () => readFrameRate(sequence));
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

/** Context for the scanned sequence (it does not have to be the active one). */
export async function getContextForSequence(sequenceGuid: string): Promise<ActiveContext> {
  return getContext(sequenceGuid);
}

function tick(t: string): TickTime {
  return ppro().TickTime.createWithTicks(t);
}

/** In/out access for the "Premiere direct" route, using the documented lockedAccess + transaction. */
export class PremiereInOutHost implements InOutHost {
  constructor(private readonly ctx: ActiveContext) {}

  async getInOut(): Promise<InOutPoints> {
    const [i, o] = await Promise.all([this.ctx.sequence.getInPoint(), this.ctx.sequence.getOutPoint()]);
    return { inTicks: ticksOf(i, "the sequence In point"), outTicks: ticksOf(o, "the sequence Out point") };
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
