/**
 * Conductor panel: music (selected audio clip) -> band -> reaction -> one parameter of the
 * selected video clip -> keyframes. Live preview; every write is one undo step.
 */
import { Audio, ChannelMode, decodeAudio, looksLikeMp3, selectChannel } from "../core/audio";
import { averageSpectrum, Spectrum } from "../core/spectrum";
import { attachEqDrag, drawEq } from "./eq";
import { BAND_PRESETS } from "../core/filters";
import { TICKS_PER_SECOND } from "../core/keyframes";
import { AudioSource, context, readFile, removeKeys, selectedAudio, selectedTarget, Target, ticksPerFrame, uiLocale, writeKeys } from "./host";
import { strings } from "./i18n";
import { analyse, Analysis, ClipTiming, CURVES, DEFAULT_SETTINGS, plan, Plan, Settings } from "./plan";
import { drawPreview } from "./preview";

const S = strings(uiLocale());
const $ = <T extends HTMLElement = HTMLInputElement>(id: string) => document.getElementById(id) as T;
const num = (id: string, fallback: number) => {
  const v = Number($(id).value);
  return Number.isFinite(v) ? v : fallback;
};

let source: AudioSource | null = null;
let audio: Audio | null = null;
let spectrum: Spectrum | null = null;
let target: Target | null = null;
let tpf = TICKS_PER_SECOND / 25;
let analysis: Analysis | null = null;
let analysisKey = "";
let current: Plan | null = null;
let timing: ClipTiming | null = null;

function status(msg: string) {
  $("status").textContent = msg;
}

function fill(id: string, entries: [string, string][], value?: string) {
  const sel = $<HTMLSelectElement>(id);
  sel.innerHTML = "";
  for (const [v, label] of entries) {
    const o = document.createElement("option");
    o.value = v;
    o.textContent = label;
    sel.appendChild(o);
  }
  if (value !== undefined) sel.value = value;
}

function settings(): Settings {
  const curve = (id: string) => CURVES[$<HTMLSelectElement>(id).value] ?? CURVES.linear;
  const velocity = $<HTMLSelectElement>("amp").value === "velocity";
  const ms = (id: string, d: number) => num(id, d) / 1000;
  return {
    ...DEFAULT_SETTINGS,
    band: { low: num("low", 40), high: num("high", 120) },
    channel: $<HTMLSelectElement>("channel").value as ChannelMode,
    mode: $<HTMLSelectElement>("mode").value as Settings["mode"],
    onsets: { sensitivityDb: num("sens", 3), minGap: ms("gap", 80) },
    trigger: {
      rise: ms("rise", 40), hold: ms("hold", 0), release: ms("rel", 250), peakOffset: ms("offset", 0),
      riseCurve: curve("riseCurve"), fallCurve: curve("fallCurve"),
      amplitude: velocity ? { mode: "velocity", minDb: -30, maxDb: -3, minAmp: 0.2, curve: CURVES.linear } : { mode: "fixed" },
      overlap: $<HTMLSelectElement>("overlap").value as "max" | "add" | "restart",
    },
    continuous: {
      inMinDb: num("inMin", -40), inMaxDb: num("inMax", -6), curve: curve("curve"),
      attack: ms("att", 5), release: ms("srel", 120), invert: $("invert").checked,
    },
    outMin: num("outMin", 100),
    outMax: num("outMax", 120),
    density: $<HTMLSelectElement>("density").value as Settings["density"],
    tolerance: Math.max(0, num("tol", 1)) / 100,
  };
}

// ---------------------------------------------------------------- persistence (per user)
const STORE = "conductor.settings.v1";
const FIELDS = ["band", "low", "high", "channel", "mode", "sens", "gap", "rise", "hold", "rel", "offset", "riseCurve",
  "fallCurve", "amp", "overlap", "inMin", "inMax", "curve", "att", "srel", "outMin", "outMax", "density", "tol"];

function save() {
  try {
    const o: Record<string, string | boolean> = { invert: $("invert").checked };
    for (const f of FIELDS) o[f] = $(f).value;
    localStorage.setItem(STORE, JSON.stringify(o));
  } catch {
    /* storage is a convenience only */
  }
}

function restore() {
  try {
    const o = JSON.parse(localStorage.getItem(STORE) ?? "{}");
    for (const f of FIELDS) if (typeof o[f] === "string") $(f).value = o[f];
    if (typeof o.invert === "boolean") $("invert").checked = o.invert;
    for (const f of ["low", "high"]) if (!Number.isFinite(Number($(f).value))) {
      $("low").value = "40";
      $("high").value = "120";
      $<HTMLSelectElement>("band").value = "kick";
    }
  } catch {
    /* ignore */
  }
}

// ---------------------------------------------------------------- recompute
function recompute() {
  const s = settings();
  $("trig").className = s.mode === "triggered" ? "" : "hidden";
  $("cont").className = s.mode === "continuous" ? "" : "hidden";
  current = null;
  timing = null;
  if (audio && source) {
    const key = JSON.stringify([source.path, source.inPoint, source.outPoint, s.band, s.channel, s.onsets]);
    if (key !== analysisKey) {
      analysis = analyse(audio, s, source.inPoint / TICKS_PER_SECOND, source.outPoint / TICKS_PER_SECOND);
      analysisKey = key;
    }
    const tStart = target ? target.start : source.start;
    const tEnd = target ? target.end : source.start + (source.outPoint - source.inPoint);
    timing = {
      audioStart: source.start, audioIn: source.inPoint, audioOut: source.outPoint,
      targetStart: tStart, targetEnd: tEnd, targetIn: target ? target.inPoint : tStart, ticksPerFrame: tpf,
    };
    current = plan(analysis!, s, timing);
    $("summary").textContent = S.summary(current.hitTimes.length, current.keys.length, current.full.length);
  } else {
    $("summary").textContent = "";
  }
  try {
    drawEq($<HTMLCanvasElement>("eq"), spectrum, s.band);
  } catch {
    /* optional */
  }
  const canvas = $<HTMLCanvasElement>("graph");
  try {
    drawPreview(canvas, analysis, current, timing, s.outMin, s.outMax);
  } catch {
    /* preview is optional */
  }
  save();
}

let pending: ReturnType<typeof setTimeout> | null = null;
function schedule() {
  if (pending) clearTimeout(pending);
  pending = setTimeout(() => {
    pending = null;
    try {
      recompute();
    } catch (e) {
      status(S.failed + String(e));
    }
  }, 300);
}

// ---------------------------------------------------------------- actions
let loadToken = 0;

type Mp3Fn = (b: ArrayBuffer, o: {
  onProgress?: (p: { fraction: number; readyMs: number }) => void;
  isCancelled?: () => boolean;
}) => Promise<Audio>;

async function useAudio() {
  const token = ++loadToken;  // a newer click cancels an unfinished load
  status(S.working);
  source = await selectedAudio();
  audio = null;
  spectrum = null;
  analysis = null;
  analysisKey = "";
  if (!source) {
    $("audio-info").textContent = S.noAudio;
    status("");
    return schedule();
  }
  const src = source;
  const { sequence } = await context();
  tpf = await ticksPerFrame(sequence);
  const secs = (src.outPoint - src.inPoint) / TICKS_PER_SECOND;
  const info = (extra: string) => {
    if (token === loadToken) $("audio-info").textContent = `${src.name} · ${secs.toFixed(1)} s${extra}`;
  };
  info("");
  const t0 = Date.now();
  try {
    const data = await readFile(src.path);
    let decoded: Audio;
    if (looksLikeMp3(data)) {
      const mp3 = (globalThis as unknown as { ConductorMp3?: { decodeMp3: Mp3Fn } }).ConductorMp3?.decodeMp3;
      if (!mp3) throw new Error("MP3 decoder not loaded");
      decoded = await mp3(data, {
        onProgress: (p) => info(` · ${S.decoding} ${Math.round(p.fraction * 100)} %`),
        isCancelled: () => token !== loadToken,
      });
    } else {
      decoded = decodeAudio(data);
    }
    if (token !== loadToken) return;
    const mix = decoded.channels.length > 1 ? selectChannel(decoded, "mix") : decoded.channels[0];
    const a = Math.max(0, Math.floor((src.inPoint / TICKS_PER_SECOND) * decoded.sampleRate));
    const b = Math.min(mix.length, Math.ceil((src.outPoint / TICKS_PER_SECOND) * decoded.sampleRate));
    spectrum = averageSpectrum(mix.subarray(a, Math.max(a + 4096, b)), decoded.sampleRate);
    audio = decoded;
    info(` · ${decoded.sampleRate} Hz · ${((Date.now() - t0) / 1000).toFixed(1)} s ✓`);
    status("");
  } catch (e) {
    if (token !== loadToken) return;
    const ext = src.path.split(".").pop()?.toLowerCase() ?? "";
    const msg = ["wav", "wave", "aif", "aiff", "mp3"].includes(ext) ? S.decodeFailed + String(e instanceof Error ? e.message : e) : S.unsupported(ext);
    $("audio-info").textContent = `${src.name}: ${msg}`;
    status(msg);
  }
  schedule();
}

async function useTarget() {
  status(S.working);
  target = await selectedTarget();
  if (!target) {
    $("target-info").textContent = S.noTarget;
    fill("param", []);
    status("");
    return schedule();
  }
  $("target-info").textContent = target.name;
  fill("param", target.params.map((p, i) => [String(i), p.label]));
  const scale = target.params.findIndex((p) => p.componentIndex >= 0 && /scal|skal/i.test(p.label));
  if (scale >= 0) $<HTMLSelectElement>("param").value = String(scale);
  paramChanged();
  status("");
}

function paramChanged() {
  if (!target) return;
  const p = target.params[Number($<HTMLSelectElement>("param").value)];
  if (!p) return;
  $("outMin").value = String(Math.round(p.value * 1000) / 1000);
  $("outMax").value = String(Math.round((p.value + Math.max(1, Math.abs(p.value) * 0.2)) * 1000) / 1000);
  schedule();
}

function keyRange(): [number, number] {
  const t = target!;
  return [t.inPoint, t.inPoint + (t.end - t.start)];
}

async function write() {
  recompute();
  if (!source) return status(S.needAudio);
  if (!audio) return status(S.audioNotReadable);
  if (!target) return status(S.needTarget);
  const choice = target.params[Number($<HTMLSelectElement>("param").value)];
  if (!choice) return status(S.needParam);
  if (!current || !timing) return status(S.failed + "no plan");
  status(S.working);
  const [from, to] = keyRange();
  const ok = await writeKeys(target, choice, current.keys, from, to, `Conductor: ${choice.label}`);
  status(ok ? S.written(current.keys.length) : S.failed + "transaction");
}

async function remove() {
  if (!target) return status(S.needTarget);
  const choice = target.params[Number($<HTMLSelectElement>("param").value)];
  if (!choice) return status(S.needParam);
  const [from, to] = keyRange();
  const ok = await removeKeys(target, choice, from, to, `Conductor: ${choice.label}`);
  status(ok ? S.removed : S.failed + "transaction");
}

function guard(fn: () => Promise<void> | void) {
  return async () => {
    try {
      await fn();
    } catch (e) {
      status(S.failed + String(e instanceof Error ? e.message : e));
    }
  };
}

document.addEventListener("DOMContentLoaded", () => {
  document.querySelectorAll("[data-t]").forEach((el) => {
    const k = el.getAttribute("data-t") as keyof typeof S;
    const v = S[k];
    if (typeof v === "string") el.textContent = v;
  });
  const curves = Object.keys(CURVES).map((k) => [k, S.curves[k] ?? k] as [string, string]);
  fill("band", [...Object.keys(BAND_PRESETS).map((k) => [k, S.bands[k] ?? k] as [string, string]), ["custom", S.bands.custom]], "kick");
  fill("channel", Object.entries(S.channels), "mix");
  fill("mode", [["triggered", S.modeTriggered], ["continuous", S.modeContinuous]], "triggered");
  fill("riseCurve", curves, "easeIn");
  fill("fallCurve", curves, "easeOut");
  fill("curve", curves, "linear");
  fill("amp", [["fixed", S.ampFixed], ["velocity", S.ampVelocity]], "fixed");
  fill("overlap", [["max", S.overlapMax], ["add", S.overlapAdd], ["restart", S.overlapRestart]], "max");
  fill("density", [["optimised", S.densityOptimised], ["frame", S.densityFrame]], "optimised");
  $("low").value = "40";
  $("high").value = "120";
  restore();
  $("band").addEventListener("change", () => {
    const b = BAND_PRESETS[$<HTMLSelectElement>("band").value];
    if (b) {
      $("low").value = String(b.low);
      $("high").value = String(b.high);
    }
    schedule();
  });
  for (const id of ["low", "high"]) $(id).addEventListener("input", () => {
    $<HTMLSelectElement>("band").value = "custom";
    schedule();
  });
  for (const el of Array.from(document.querySelectorAll("input, select")))
    if (!["band", "low", "high", "param"].includes(el.id)) el.addEventListener(el.tagName === "SELECT" || (el as HTMLInputElement).type === "checkbox" ? "change" : "input", schedule);
  $("param").addEventListener("change", paramChanged);
  attachEqDrag(
    $<HTMLCanvasElement>("eq"),
    () => ({ low: num("low", 0), high: num("high", 0) }),
    (b) => {
      $("low").value = String(b.low);
      $("high").value = String(b.high);
      $<HTMLSelectElement>("band").value = "custom";
      try {
        drawEq($<HTMLCanvasElement>("eq"), spectrum, b);
      } catch {
        /* optional */
      }
      schedule();
    },
  );
  $("btn-audio").addEventListener("click", guard(useAudio));
  $("btn-target").addEventListener("click", guard(useTarget));
  $("btn-write").addEventListener("click", guard(write));
  $("btn-remove").addEventListener("click", guard(remove));
  schedule();
});
