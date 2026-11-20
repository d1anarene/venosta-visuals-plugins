/**
 * Conductor C0 feasibility spike (research only, throwaway projects).
 *
 * Answers, in a real Premiere Pro, the questions of plugins/conductor/AGENTS.md §5:
 *   1. Can a UXP panel write keyframes onto any effect parameter, how fast, in which time base?
 *   2. Is speed / time remapping reachable as a keyframable parameter?
 *   3. Can the panel read an audio clip's media file (and decode WAV)?
 *   4. Can it store settings (plugin data folder)?
 * Everything is logged into the panel; "Copy Log" puts it on the clipboard for the owner to send.
 * API: @adobe/premierepro 27.0 typings; every call is guarded because nothing here is verified yet.
 */
import type {
  Component,
  ComponentParam,
  premierepro,
  Project,
  Sequence,
  TickTime,
  VideoClipTrackItem,
  AudioClipTrackItem,
} from "@adobe/premierepro";
import { pulseValues } from "./pulse";
import { parseWav, rmsEnvelopeDb } from "./wav";

/* eslint-disable @typescript-eslint/no-explicit-any */
declare const require: (id: string) => any;

const TICKS_PER_SECOND = 254016000000;
let P: premierepro | null = null;
const ppro = (): premierepro => (P ??= require("premierepro") as premierepro);

// ---------------------------------------------------------------- log
const lines: string[] = [];
function log(msg: string, data?: unknown) {
  let s = `[${new Date().toISOString().slice(11, 23)}] ${msg}`;
  if (data !== undefined) {
    try {
      s += " " + (data instanceof Error ? `${data.name}: ${data.message}` : typeof data === "string" ? data : JSON.stringify(data));
    } catch {
      s += " " + String(data);
    }
  }
  lines.push(s);
  const el = document.getElementById("log") as HTMLTextAreaElement | null;
  if (el) {
    el.value = lines.join("\n");
    el.scrollTop = el.scrollHeight;
  }
}
const ms = (t0: number) => `${(Date.now() - t0).toFixed(0)} ms`;
const secs = (t: TickTime | null | undefined) => {
  if (!t) return "?";
  try {
    return `${Number(t.ticks) / TICKS_PER_SECOND}s`;
  } catch {
    return "?";
  }
};
const tick = (ticks: number): TickTime => ppro().TickTime.createWithTicks(String(Math.round(ticks)));
const ticksOf = (t: TickTime): number => Number(t.ticks);

// ---------------------------------------------------------------- context
async function context(): Promise<{ project: Project; sequence: Sequence }> {
  const project = await ppro().Project.getActiveProject();
  if (!project) throw new Error("no active project");
  const sequence = await project.getActiveSequence();
  if (!sequence) throw new Error("no active sequence");
  return { project, sequence };
}

type Item = { kind: "video" | "audio"; item: VideoClipTrackItem | AudioClipTrackItem; name: string; track: number };

/** Selected clips, classified by the track list they were found in (video or audio). */
async function selectedItems(sequence: Sequence): Promise<Item[]> {
  const out: Item[] = [];
  const CLIP = ppro().Constants.TrackItemType.CLIP;
  const vCount = await sequence.getVideoTrackCount();
  for (let t = 0; t < vCount; ++t) {
    const track = await sequence.getVideoTrack(t);
    for (const it of track.getTrackItems(CLIP, false) ?? []) {
      if (await it.getIsSelected()) out.push({ kind: "video", item: it, name: await it.getName(), track: t });
    }
  }
  const aCount = await sequence.getAudioTrackCount();
  for (let t = 0; t < aCount; ++t) {
    const track = await sequence.getAudioTrack(t);
    for (const it of track.getTrackItems(CLIP, false) ?? []) {
      if (await it.getIsSelected()) out.push({ kind: "audio", item: it, name: await it.getName(), track: t });
    }
  }
  return out;
}

async function ticksPerFrame(sequence: Sequence): Promise<number> {
  try {
    const tb = Number(await sequence.getTimebase());
    if (Number.isFinite(tb) && tb > 0) return tb;
  } catch (e) {
    log("getTimebase failed", e);
  }
  log("frame rate unknown, assuming 25 fps");
  return TICKS_PER_SECOND / 25;
}

async function startValue(p: ComponentParam): Promise<unknown> {
  try {
    const v: any = await p.getStartValue();
    if (v && typeof v === "object" && "value" in v) return (v as any).value?.value ?? (v as any).value;
    return v;
  } catch (e) {
    return `error ${e}`;
  }
}

// ---------------------------------------------------------------- 1. inspect
type ParamRef = { item: Item; comp: Component; compName: string; param: ComponentParam; label: string };
let params: ParamRef[] = [];

async function inspect() {
  log("=== 1. Inspect selection ===");
  try {
    const host = require("uxp").host;
    log("host", { app: host?.name, version: host?.version, uiLocale: host?.uiLocale, os: require("os")?.platform?.() });
  } catch (e) {
    log("host info failed", e);
  }
  const { sequence } = await context();
  log("sequence", { name: sequence.name, ticksPerFrame: await ticksPerFrame(sequence) });
  const items = await selectedItems(sequence);
  log(`selected clips: ${items.length}`);
  params = [];
  for (const it of items) {
    const i = it.item as VideoClipTrackItem;
    const info: Record<string, unknown> = { kind: it.kind, name: it.name, track: it.track };
    for (const [k, f] of [
      ["start", () => i.getStartTime()],
      ["end", () => i.getEndTime()],
      ["in", () => i.getInPoint()],
      ["out", () => i.getOutPoint()],
    ] as const) {
      try {
        info[k] = secs(await (f as () => Promise<TickTime>)());
      } catch (e) {
        info[k] = `error ${e}`;
      }
    }
    try {
      info.speed = await i.getSpeed();
      info.reversed = await i.isSpeedReversed();
    } catch (e) {
      info.speed = `error ${e}`;
    }
    log("clip", info);
    let chain: any;
    try {
      chain = await (it.item as any).getComponentChain();
    } catch (e) {
      log("  getComponentChain failed", e);
      continue;
    }
    const n = chain.getComponentCount();
    for (let c = 0; c < n; ++c) {
      const comp: Component = chain.getComponentAtIndex(c);
      let compName = "?", match = "?";
      try {
        compName = await comp.getDisplayName();
        match = await comp.getMatchName();
      } catch (e) {
        log("  component name failed", e);
      }
      const pc = comp.getParamCount();
      log(`  [${c}] ${compName} (${match}), ${pc} params`);
      for (let j = 0; j < pc; ++j) {
        const p = comp.getParam(j);
        let kf: unknown = "?", tv: unknown = "?";
        try {
          kf = await p.areKeyframesSupported();
        } catch (e) {
          kf = `error ${e}`;
        }
        try {
          tv = p.isTimeVarying();
        } catch (e) {
          tv = `error ${e}`;
        }
        const v = await startValue(p);
        log(`      (${j}) ${p.displayName} keyframable=${kf} timeVarying=${tv} value=${JSON.stringify(v)}`);
        if (it.kind === "video" && kf === true) {
          params.push({ item: it, comp, compName, param: p, label: `${it.name} / ${compName} / ${p.displayName}` });
        }
      }
    }
  }
  const picker = document.getElementById("param-picker") as HTMLSelectElement;
  picker.innerHTML = "";
  params.forEach((r, k) => {
    const o = document.createElement("option");
    o.value = String(k);
    o.textContent = r.label;
    picker.appendChild(o);
  });
  if (!params.length) {
    const o = document.createElement("option");
    o.value = "";
    o.textContent = "- no keyframable video parameter found -";
    picker.appendChild(o);
  }
  log(`keyframable video parameters: ${params.length}`);
}

// ---------------------------------------------------------------- 2. keyframes
function selectedParam(): ParamRef | null {
  const v = (document.getElementById("param-picker") as HTMLSelectElement).value;
  return v === "" ? null : params[Number(v)] ?? null;
}
const choice = (id: string) => (document.getElementById(id) as HTMLSelectElement).value;

async function originTicks(ref: ParamRef, base: string): Promise<number> {
  const i = ref.item.item as VideoClipTrackItem;
  if (base === "in") return ticksOf(await i.getInPoint());
  if (base === "start") return ticksOf(await i.getStartTime());
  return 0;
}

async function writeKeyframes() {
  log("=== 2. Write keyframes ===");
  const ref = selectedParam();
  if (!ref) return log("pick a parameter (run step 1 first)");
  const { project, sequence } = await context();
  const tpf = await ticksPerFrame(sequence);
  const base = choice("timebase");
  const t0 = await originTicks(ref, base);
  const durTicks = ticksOf(await (ref.item.item as VideoClipTrackItem).getDuration());
  const want = Number(choice("count"));
  const amp = Number((document.getElementById("amp") as HTMLInputElement).value) || 20;
  const start = await startValue(ref.param);
  let baseValue: number;
  try {
    const at: any = await ref.param.getValueAtTime(tick(t0));
    baseValue = Number(at?.value);
  } catch {
    baseValue = Number(start);
  }
  if (!Number.isFinite(baseValue)) return log("parameter is not a plain number; this test writes numbers only", { start });
  const maxFrames = Math.max(1, Math.floor(durTicks / tpf));
  const count = want === 1 ? 1 : Math.min(want, maxFrames);
  if (count < want) log(`clip is ${maxFrames} frames long: writing ${count} keyframes instead of ${want}`);
  const values = want === 1 ? [baseValue + amp] : pulseValues(count, baseValue, amp);
  const times = want === 1 ? [t0 + TICKS_PER_SECOND] : values.map((_, k) => t0 + k * tpf);
  log("plan", { param: ref.label, timeBase: base, originSeconds: t0 / TICKS_PER_SECOND, count, baseValue, amp });

  const tA = Date.now();
  let ok = false;
  try {
    project.lockedAccess(() => {
      ok = project.executeTransaction((compound) => {
        if (!ref.param.isTimeVarying()) compound.addAction(ref.param.createSetTimeVaryingAction(true));
        for (let k = 0; k < values.length; ++k) {
          const kf = ref.param.createKeyframe(values[k]);
          kf.position = tick(times[k]);
          compound.addAction(ref.param.createAddKeyframeAction(kf));
        }
      }, "Conductor test: write keyframes");
    });
  } catch (e) {
    log("transaction threw", e);
  }
  log(`write transaction: ok=${ok}, ${ms(tA)}`);

  const mode = choice("interp");
  if (ok && mode !== "linear") {
    const K: any = ppro().Keyframe;
    const m = mode === "bezier" ? K.INTERPOLATION_MODE_BEZIER : K.INTERPOLATION_MODE_HOLD;
    const tB = Date.now();
    let ok2 = false;
    try {
      project.lockedAccess(() => {
        ok2 = project.executeTransaction((compound) => {
          for (const t of times) compound.addAction(ref.param.createSetInterpolationAtKeyframeAction(tick(t), m));
        }, "Conductor test: interpolation");
      });
    } catch (e) {
      log("interpolation transaction threw", e);
    }
    log(`interpolation (${mode}) transaction: ok=${ok2}, ${ms(tB)}`);
  }

  try {
    const list = ref.param.getKeyframeListAsTickTimes() ?? [];
    log(`read back: ${list.length} keyframes, first ${secs(list[0])}, last ${secs(list[list.length - 1])}`);
    for (const k of [0, Math.floor(times.length / 2)]) {
      const v: any = await ref.param.getValueAtTime(tick(times[k]));
      log(`  value at written key ${k} (${times[k] / TICKS_PER_SECOND}s): ${JSON.stringify(v?.value)} (wanted ${values[k]})`);
    }
  } catch (e) {
    log("read back failed", e);
  }
  log("Now look at Effect Controls: where do the keyframes sit relative to the clip? Undo once: are all gone?");
}

async function removeKeyframes() {
  log("=== 2b. Remove keyframes ===");
  const ref = selectedParam();
  if (!ref) return log("pick a parameter first");
  const { project } = await context();
  const i = ref.item.item as VideoClipTrackItem;
  const end = Math.max(ticksOf(await i.getOutPoint()), ticksOf(await i.getEndTime())) + 600 * TICKS_PER_SECOND;
  const tA = Date.now();
  let ok = false;
  try {
    project.lockedAccess(() => {
      ok = project.executeTransaction((compound) => {
        compound.addAction(ref.param.createRemoveKeyframeRangeAction(tick(0), tick(end)));
      }, "Conductor test: remove keyframes");
    });
  } catch (e) {
    log("remove threw", e);
  }
  let left = -1;
  try {
    left = (ref.param.getKeyframeListAsTickTimes() ?? []).length;
  } catch {
    /* ignore */
  }
  log(`remove: ok=${ok}, ${ms(tA)}, keyframes left: ${left}`);
}

// ---------------------------------------------------------------- 3. time remapping
async function timeRemap() {
  log("=== 3. Time remapping ===");
  const { project, sequence } = await context();
  const items = (await selectedItems(sequence)).filter((x) => x.kind === "video");
  if (!items.length) return log("select the VIDEO clip in the timeline (alone or together with audio), then press again");
  const it = items[0];
  const i = it.item as VideoClipTrackItem;
  log("before", { speed: await i.getSpeed().catch((e) => `error ${e}`), duration: secs(await i.getDuration()) });
  const chain: any = await i.getComponentChain();
  const re = /speed|remap|geschwindigkeit|zeitverzerrung|time/i;
  let target: { comp: Component; param: ComponentParam; name: string } | null = null;
  for (let c = 0; c < chain.getComponentCount(); ++c) {
    const comp: Component = chain.getComponentAtIndex(c);
    const cname = await comp.getDisplayName().catch(() => "?");
    const match = await comp.getMatchName().catch(() => "?");
    for (let j = 0; j < comp.getParamCount(); ++j) {
      const p = comp.getParam(j);
      if (re.test(p.displayName) || re.test(cname) || re.test(match)) {
        const kf = await p.areKeyframesSupported().catch(() => "?");
        log(`candidate: [${c}] ${cname} (${match}) / (${j}) ${p.displayName} keyframable=${kf} value=${JSON.stringify(await startValue(p))}`);
        if (!target && kf === true && re.test(p.displayName)) target = { comp, param: p, name: `${cname} / ${p.displayName}` };
      }
    }
  }
  if (!target) return log("no keyframable speed / time remapping parameter visible to UXP on this clip");
  const t0 = ticksOf(await i.getInPoint());
  const base = Number(await startValue(target.param));
  if (!Number.isFinite(base)) return log("speed parameter value is not a number", { base });
  const plan = [
    [t0, base],
    [t0 + TICKS_PER_SECOND, base * 3],
    [t0 + 2 * TICKS_PER_SECOND, base],
  ];
  let ok = false;
  try {
    project.lockedAccess(() => {
      ok = project.executeTransaction((compound) => {
        if (!target!.param.isTimeVarying()) compound.addAction(target!.param.createSetTimeVaryingAction(true));
        for (const [t, v] of plan) {
          const kf = target!.param.createKeyframe(v);
          kf.position = tick(t);
          compound.addAction(target!.param.createAddKeyframeAction(kf));
        }
      }, "Conductor test: time remapping");
    });
  } catch (e) {
    log("time remapping transaction threw", e);
  }
  log(`time remapping write (${target.name}): ok=${ok}`);
  log("after", { speed: await i.getSpeed().catch((e) => `error ${e}`), duration: secs(await i.getDuration()) });
  log("Check the clip's Time Remapping rubber band in the timeline / Effect Controls and play it.");
}

// ---------------------------------------------------------------- 4. audio
function toFileUrl(nativePath: string): string {
  let p = nativePath.replace(/\\/g, "/");
  if (!p.startsWith("/")) p = "/" + p;
  return "file:" + p;
}

async function readFile(path: string): Promise<ArrayBuffer | null> {
  const uxp = require("uxp");
  const lfs = uxp.storage.localFileSystem;
  for (const url of [toFileUrl(path), encodeURI(toFileUrl(path))]) {
    try {
      const entry = await lfs.getEntryWithUrl(url);
      if (!entry) continue;
      const t = Date.now();
      const data = await entry.read({ format: uxp.storage.formats.binary });
      log(`read via localFileSystem (${url === toFileUrl(path) ? "plain" : "encoded"} URL): ${data.byteLength} bytes, ${ms(t)}`);
      return data as ArrayBuffer;
    } catch (e) {
      log(`localFileSystem read failed for ${url}`, e);
    }
  }
  try {
    const fs = require("fs");
    const t = Date.now();
    const data = fs.readFileSync(path);
    const ab: ArrayBuffer = data instanceof ArrayBuffer ? data : data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
    log(`read via require("fs"): ${ab.byteLength} bytes, ${ms(t)}`);
    return ab;
  } catch (e) {
    log('require("fs") read failed', e);
  }
  return null;
}

async function audioTest() {
  log("=== 4. Audio access ===");
  const { sequence } = await context();
  const items = await selectedItems(sequence);
  const audio = items.filter((x) => x.kind === "audio");
  if (!audio.length) return log("select an audio clip (a WAV file is best; others are only read, not decoded)");
  for (const it of audio) {
    const i = it.item as AudioClipTrackItem;
    let path = "";
    try {
      const pi = await i.getProjectItem();
      const clip = ppro().ClipProjectItem.cast(pi);
      path = await clip.getMediaFilePath();
    } catch (e) {
      log("media path failed", e);
      continue;
    }
    log("audio clip", { name: it.name, path, start: secs(await i.getStartTime()), in: secs(await i.getInPoint()), out: secs(await i.getOutPoint()) });
    const data = await readFile(path);
    if (!data) continue;
    const ext = path.split(".").pop()?.toLowerCase();
    if (ext !== "wav" && ext !== "wave") {
      log(`not a WAV file (.${ext}): readable, but decoding would need a decoder`);
      continue;
    }
    try {
      const t = Date.now();
      const info = parseWav(data);
      const env = rmsEnvelopeDb(data, info, 0.1);
      const finite = env.filter(Number.isFinite);
      log("WAV decoded", {
        ...info,
        seconds: info.frames / info.sampleRate,
        envelopeWindows: env.length,
        rmsDbMin: Math.min(...finite).toFixed(1),
        rmsDbMax: Math.max(...finite).toFixed(1),
        time: ms(t),
      });
    } catch (e) {
      log("WAV decode failed", e);
    }
  }
}

// ---------------------------------------------------------------- 5. storage
async function storageTest() {
  log("=== 5. Storage ===");
  try {
    const uxp = require("uxp");
    const folder = await uxp.storage.localFileSystem.getDataFolder();
    const file = await folder.createFile("conductor-test.json", { overwrite: true });
    const payload = JSON.stringify({ written: new Date().toISOString(), test: [1, 2, 3] });
    await file.write(payload);
    const back = await file.read();
    log("data folder", { path: folder.nativePath, roundTrip: back === payload });
  } catch (e) {
    log("data folder test failed", e);
  }
}

// ---------------------------------------------------------------- 6. WebAssembly
// A 41-byte module exporting add(i32, i32) -> i32: can UXP run WebAssembly (e.g. an MP3 decoder)?
const ADD_WASM = new Uint8Array([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01,
  0x00, 0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a,
  0x0b,
]);

async function wasmTest() {
  log("=== 6. WebAssembly ===");
  const W = (globalThis as any).WebAssembly;
  log(`typeof WebAssembly: ${typeof W}`);
  if (!W) return;
  try {
    const t = Date.now();
    const { instance } = await W.instantiate(ADD_WASM);
    log(`instantiate ok, add(2, 3) = ${instance.exports.add(2, 3)}, ${ms(t)}`);
  } catch (e) {
    log("WebAssembly.instantiate failed", e);
  }
}

// ---------------------------------------------------------------- wiring
function on(id: string, fn: () => Promise<void> | void) {
  document.getElementById(id)?.addEventListener("click", async () => {
    try {
      await fn();
    } catch (e) {
      log(`${id} failed`, e);
    }
  });
}

document.addEventListener("DOMContentLoaded", () => {
  on("btn-inspect", inspect);
  on("btn-write", writeKeyframes);
  on("btn-remove", removeKeyframes);
  on("btn-remap", timeRemap);
  on("btn-audio", audioTest);
  on("btn-storage", storageTest);
  on("btn-wasm", wasmTest);
  on("btn-clear", () => {
    lines.length = 0;
    log("log cleared");
  });
  on("btn-copy", async () => {
    await (navigator as any).clipboard.writeText(lines.join("\n"));
    log("log copied to the clipboard");
  });
  log("Conductor test panel ready (research build). Use a throwaway project.");
});
