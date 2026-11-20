/**
 * From settings + decoded audio + clip timing to the keyframes of one Conductor. Pure and
 * deterministic (unit-tested); the panel only gathers the inputs from Premiere and writes the
 * result.
 *
 * Times:
 *  - audio source seconds (position in the audio file);
 *  - timeline ticks (sequence time);
 *  - keyframe ticks of the target parameter: Premiere keeps effect keyframes in the clip's
 *    **media time** (source time of the target clip). Assumed here: key = targetIn + (T -
 *    targetStart). Owner host test pending (C0 round 3); `keyTimeBase` switches it.
 */
import { Audio, ChannelMode, selectChannel } from "../core/audio";
import { applyCurve, Curve } from "../core/curves";
import { Envelope, rmsEnvelope } from "../core/envelope";
import { Band, bandpass } from "../core/filters";
import { detectHits, Hit, OnsetOptions } from "../core/onsets";
import { continuous, ContinuousOptions, eventsFromHits, triggered, TriggerOptions } from "../core/shape";
import { Key, simplify, TICKS_PER_SECOND } from "../core/keyframes";

export interface ClipTiming {
  /** Audio clip on the timeline: start (sequence ticks) and in point (source ticks of the audio file). */
  audioStart: number;
  audioIn: number;
  audioOut: number;
  /** Target clip: start/end on the timeline and its in point (source ticks). */
  targetStart: number;
  targetEnd: number;
  targetIn: number;
  ticksPerFrame: number;
}

export interface Settings {
  band: Band;
  channel: ChannelMode;
  mode: "continuous" | "triggered";
  onsets: Partial<OnsetOptions>;
  continuous: Partial<ContinuousOptions>;
  trigger: Partial<TriggerOptions>;
  /** Value range written to the parameter. */
  outMin: number;
  outMax: number;
  density: "frame" | "optimised";
  /** Allowed deviation for "optimised", as a fraction of |outMax - outMin|. */
  tolerance: number;
  keyTimeBase: "media" | "sequence";
  /** Triggered mode: move each event peak onto the nearest sequence frame, so the written keys
   *  contain the full peak value (sampling per frame would otherwise miss short peaks by up to
   *  half a frame). Shift <= half a frame. */
  snapPeaks: boolean;
}

export const DEFAULT_SETTINGS: Settings = {
  band: { low: 40, high: 120 },
  channel: "mix",
  mode: "triggered",
  onsets: {},
  continuous: {},
  trigger: { rise: 0.04, hold: 0, release: 0.25 },
  outMin: 100,
  outMax: 120,
  density: "optimised",
  tolerance: 0.01,
  keyTimeBase: "media",
  snapPeaks: true,
};

export interface Analysis {
  /** Audio source seconds of envelope step 0. */
  offset: number;
  envelope: Envelope;
  /** Hits in audio source seconds. */
  hits: Hit[];
}

/** Band envelope + hits of the audio between `from` and `to` (source seconds, with margins). */
const channelCache = new WeakMap<Audio, Map<ChannelMode, Float32Array>>();

/** The analysis channel of `audio`, computed once per audio and mode (long songs: tens of MB). */
function channelOf(audio: Audio, mode: ChannelMode): Float32Array {
  let m = channelCache.get(audio);
  if (!m) channelCache.set(audio, (m = new Map()));
  let c = m.get(mode);
  if (!c) m.set(mode, (c = selectChannel(audio, mode)));
  return c;
}

export function analyse(audio: Audio, s: Pick<Settings, "band" | "channel" | "onsets">, from = 0, to = Infinity): Analysis {
  const all = channelOf(audio, s.channel);
  const margin = 0.5;
  const a = Math.max(0, Math.floor((from - margin) * audio.sampleRate));
  const b = Math.min(all.length, Math.ceil((to + margin) * audio.sampleRate));
  const part = all.subarray(a, Math.max(a, b));
  const env = rmsEnvelope(bandpass(part, audio.sampleRate, s.band), audio.sampleRate);
  const offset = a / audio.sampleRate;
  const hits = detectHits(env, s.onsets).map((h) => ({ ...h, onsetTime: h.onsetTime + offset, peakTime: h.peakTime + offset }));
  return { offset, envelope: env, hits };
}

/** Value function over audio source seconds. With `c`, triggered peaks snap to the frame grid. */
export function valueFunction(an: Analysis, s: Settings, c?: ClipTiming): (sourceSec: number) => number {
  if (s.mode === "continuous") {
    const f = continuous(an.envelope, { ...s.continuous, outMin: s.outMin, outMax: s.outMax });
    return (t) => f(t - an.offset);
  }
  const trig = { ...s.trigger, base: s.outMin, peak: s.outMax };
  // only hits the audio clip actually plays (its in..out range of the file)
  const hits = c ? an.hits.filter((h) => h.peakTime * TICKS_PER_SECOND >= c.audioIn && h.peakTime * TICKS_PER_SECOND < c.audioOut) : an.hits;
  let events = eventsFromHits(hits, trig);
  if (c && s.snapPeaks) {
    events = events.map((e) => {
      const T = c.audioStart + (e.at * TICKS_PER_SECOND - c.audioIn);  // timeline ticks
      const snapped = Math.round(T / c.ticksPerFrame) * c.ticksPerFrame;
      return { ...e, at: (c.audioIn + (snapped - c.audioStart)) / TICKS_PER_SECOND };
    });
  }
  return triggered(events, trig);
}

export interface Plan {
  keys: Key[];
  /** Per-frame keys before simplification (for the preview). */
  full: Key[];
  /** Hits inside the target range, in timeline seconds. */
  hitTimes: number[];
}

/** Keyframes for the target clip's timeline range, one per sequence frame (then optionally simplified). */
export function plan(an: Analysis, s: Settings, c: ClipTiming): Plan {
  const f = valueFunction(an, s, c);
  const toSource = (T: number) => (c.audioIn + (T - c.audioStart)) / TICKS_PER_SECOND;
  const toKey = (T: number) => (s.keyTimeBase === "media" ? c.targetIn + (T - c.targetStart) : T);
  // frames on the sequence grid inside [targetStart, targetEnd)
  const first = Math.ceil(c.targetStart / c.ticksPerFrame) * c.ticksPerFrame;
  const full: Key[] = [];
  for (let T = first; T < c.targetEnd; T += c.ticksPerFrame) full.push({ ticks: toKey(T), value: f(toSource(T)) });
  const span = Math.abs(s.outMax - s.outMin) || 1;
  const keys = s.density === "optimised" ? simplify(full, s.tolerance * span) : full;
  const audioEnd = c.audioStart + (c.audioOut - c.audioIn);
  const hitTimes = an.hits
    .map((h) => (c.audioStart + (h.peakTime * TICKS_PER_SECOND - c.audioIn)) / TICKS_PER_SECOND)
    .filter((t) => t * TICKS_PER_SECOND >= Math.max(c.targetStart, c.audioStart) && t * TICKS_PER_SECOND < Math.min(c.targetEnd, audioEnd));
  return { keys, full, hitTimes };
}

/** Shape curves offered in the panel (ids are stored in settings; labels are localised). */
export const CURVES: Record<string, Curve> = {
  linear: { kind: "linear" },
  easeIn: { kind: "easeIn" },
  easeOut: { kind: "easeOut" },
  easeInOut: { kind: "easeInOut" },
  expIn: { kind: "exp", k: 4 },
  expOut: { kind: "exp", k: -4 },
  snap: { kind: "bezier", x1: 0.1, y1: 0.9, x2: 0.2, y2: 1 },
};

export function curveSample(id: string, u: number): number {
  return applyCurve(CURVES[id] ?? CURVES.linear, u);
}
