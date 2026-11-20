/**
 * The only part that touches Premiere (UXP, @adobe/premierepro 27.0 typings). Host-tested in C0:
 * selection, component chains, keyframe transactions, media paths, file reads.
 */
import type { AudioClipTrackItem, ComponentParam, premierepro, Project, Sequence, TickTime, VideoClipTrackItem } from "@adobe/premierepro";
import type { Key } from "../core/keyframes";

/* eslint-disable @typescript-eslint/no-explicit-any */
declare const require: (id: string) => any;

let P: premierepro | null = null;
export const ppro = (): premierepro => (P ??= require("premierepro") as premierepro);
export const uiLocale = (): string => {
  try {
    return require("uxp").host.uiLocale ?? "en_US";
  } catch {
    return "en_US";
  }
};

const ticksOf = (t: TickTime): number => Number(t.ticks);
const tick = (n: number): TickTime => ppro().TickTime.createWithTicks(String(Math.round(n)));

export async function context(): Promise<{ project: Project; sequence: Sequence }> {
  const project = await ppro().Project.getActiveProject();
  if (!project) throw new Error("no active project");
  const sequence = await project.getActiveSequence();
  if (!sequence) throw new Error("no active sequence");
  return { project, sequence };
}

export async function ticksPerFrame(sequence: Sequence): Promise<number> {
  const tb = Number(await sequence.getTimebase());
  return Number.isFinite(tb) && tb > 0 ? tb : 254016000000 / 25;
}

async function selected<T>(sequence: Sequence, video: boolean): Promise<T[]> {
  const CLIP = ppro().Constants.TrackItemType.CLIP;
  const count = video ? await sequence.getVideoTrackCount() : await sequence.getAudioTrackCount();
  const out: T[] = [];
  for (let t = 0; t < count; ++t) {
    const track: any = video ? await sequence.getVideoTrack(t) : await sequence.getAudioTrack(t);
    for (const it of track.getTrackItems(CLIP, false) ?? []) if (await it.getIsSelected()) out.push(it as T);
  }
  return out;
}

export interface AudioSource {
  name: string;
  path: string;
  start: number;
  inPoint: number;
  outPoint: number;
  speed: number;
}

export async function selectedAudio(): Promise<AudioSource | null> {
  const { sequence } = await context();
  const [it] = await selected<AudioClipTrackItem>(sequence, false);
  if (!it) return null;
  const clip = ppro().ClipProjectItem.cast(await it.getProjectItem());
  return {
    name: await it.getName(),
    path: await clip.getMediaFilePath(),
    start: ticksOf(await it.getStartTime()),
    inPoint: ticksOf(await it.getInPoint()),
    outPoint: ticksOf(await it.getOutPoint()),
    speed: await it.getSpeed(),
  };
}

export interface ParamChoice {
  label: string;
  componentIndex: number;
  paramIndex: number;
  value: number;
}

export interface Target {
  item: VideoClipTrackItem;
  name: string;
  start: number;
  end: number;
  inPoint: number;
  speed: number;
  params: ParamChoice[];
}

/** First selected video clip with its numeric keyframable parameters. */
export async function selectedTarget(): Promise<Target | null> {
  const { sequence } = await context();
  const [it] = await selected<VideoClipTrackItem>(sequence, true);
  if (!it) return null;
  const chain: any = await it.getComponentChain();
  const params: ParamChoice[] = [];
  for (let c = 0; c < chain.getComponentCount(); ++c) {
    const comp = chain.getComponentAtIndex(c);
    const cname = await comp.getDisplayName();
    for (let j = 0; j < comp.getParamCount(); ++j) {
      const p: ComponentParam = comp.getParam(j);
      try {
        if (!(await p.areKeyframesSupported()) || !p.displayName.trim()) continue;
        const v: any = await p.getStartValue();
        const value = v?.value?.value ?? v?.value;
        if (typeof value === "number") params.push({ label: `${cname} › ${p.displayName}`, componentIndex: c, paramIndex: j, value });
      } catch {
        /* skip parameters that cannot be read */
      }
    }
  }
  return {
    item: it,
    name: await it.getName(),
    start: ticksOf(await it.getStartTime()),
    end: ticksOf(await it.getEndTime()),
    inPoint: ticksOf(await it.getInPoint()),
    speed: await it.getSpeed(),
    params,
  };
}

async function paramOf(t: Target, choice: ParamChoice): Promise<ComponentParam> {
  const chain: any = await t.item.getComponentChain();
  return chain.getComponentAtIndex(choice.componentIndex).getParam(choice.paramIndex);
}

/** Replaces the parameter's keyframes in [from, to] (key ticks) with `keys`, as one undo step. */
export async function writeKeys(t: Target, choice: ParamChoice, keys: Key[], from: number, to: number, label: string): Promise<boolean> {
  const { project } = await context();
  const p = await paramOf(t, choice);
  let ok = false;
  project.lockedAccess(() => {
    ok = project.executeTransaction((compound) => {
      if (!p.isTimeVarying()) compound.addAction(p.createSetTimeVaryingAction(true));
      else compound.addAction(p.createRemoveKeyframeRangeAction(tick(from), tick(to)));
      for (const k of keys) {
        const kf = p.createKeyframe(k.value);
        kf.position = tick(k.ticks);
        compound.addAction(p.createAddKeyframeAction(kf));
      }
    }, label);
  });
  return ok;
}

export async function removeKeys(t: Target, choice: ParamChoice, from: number, to: number, label: string): Promise<boolean> {
  const { project } = await context();
  const p = await paramOf(t, choice);
  let ok = false;
  project.lockedAccess(() => {
    ok = project.executeTransaction((compound) => {
      compound.addAction(p.createRemoveKeyframeRangeAction(tick(from), tick(to)));
    }, label);
  });
  return ok;
}

function toFileUrl(nativePath: string): string {
  let p = nativePath.replace(/\\/g, "/");
  if (!p.startsWith("/")) p = "/" + p;
  return "file:" + p;
}

export async function readFile(path: string): Promise<ArrayBuffer> {
  const uxp = require("uxp");
  const lfs = uxp.storage.localFileSystem;
  let last: unknown = null;
  for (const url of [toFileUrl(path), encodeURI(toFileUrl(path))]) {
    try {
      const entry = await lfs.getEntryWithUrl(url);
      if (entry) return (await entry.read({ format: uxp.storage.formats.binary })) as ArrayBuffer;
    } catch (e) {
      last = e;
    }
  }
  throw last ?? new Error("file not found");
}
