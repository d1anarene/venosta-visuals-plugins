/**
 * Reading and patching Adobe Media Encoder preset files (.epr).
 *
 * An .epr is XML (`<PremiereData>`). Every encoder setting is an `<ExporterParam>` block with a
 * `<ParamIdentifier>` (e.g. ADBEVideoFPS, ADBEVideoTargetBitrate, ADBEVideoWidth) and usually a
 * `<ParamValue>`. Observed in Adobe's own sample preset (uxp-premiere-pro-samples, "HD 1080i 25.epr"):
 *   ADBEVideoFPS            ParamValue = ticks per frame (10160640000 = 25 fps)
 *   ADBEVideoTargetBitrate  ParamValue = Mbps as float ("15.")
 *   ADBEVideoWidth/Height   ParamValue = pixels
 *   ADBEAudioBitrate        ParamValue = kbps
 *   ParamType               2 = integer, 3 = float, 4 = ticks (values are written in that format)
 *   <DoAudio>/<DoVideo>     true/false
 *   <ExporterFileType>      four-character format code as integer ('mpg2' = 1836082994)
 *
 * We never build presets from scratch: we copy one of Media Encoder's own presets installed on the
 * user's computer and change only parameters that exist in it. Anything not present is reported
 * as "not applied" instead of being invented.
 */

export interface EprInfo {
  /** English default name from the localisation key, or the raw name. */
  name: string;
  /** Four-character file type, e.g. "H264", "HEVC", "MooV", "mpg2", or "" if unknown. */
  fileType: string;
}

const PARAM_BLOCK = /<ExporterParam\b[^>]*>[\s\S]*?<\/ExporterParam>/g;

function escapeXml(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

/** Integer four-character code -> string (big-endian), e.g. 1836082994 -> "mpg2". */
export function fourCC(n: number): string {
  if (!Number.isFinite(n) || n <= 0 || n > 0xffffffff) return "";
  let s = "";
  for (let shift = 24; shift >= 0; shift -= 8) s += String.fromCharCode((n >>> shift) & 0xff);
  return /^[\x20-\x7e]{4}$/.test(s) ? s : "";
}

function tagText(xml: string, tag: string): string | null {
  const m = new RegExp(`<${tag}>([\\s\\S]*?)</${tag}>`).exec(xml);
  return m ? m[1] : null;
}

/** Display name: "($$$/AME/.../PresetName=HD 1080i 25)" -> "HD 1080i 25". */
export function presetDisplayName(raw: string): string {
  const m = /=([^)]*)\)\s*$/.exec(raw.trim());
  return (m ? m[1] : raw).trim();
}

export function readEprInfo(xml: string): EprInfo | null {
  if (!/<PremiereData\b/.test(xml)) return null;
  const name = presetDisplayName(tagText(xml, "PresetName") ?? "");
  const ft = Number(tagText(xml, "ExporterFileType") ?? "");
  return { name, fileType: fourCC(ft) };
}

/** Value of a parameter, or undefined if the parameter (or its value) is absent. */
export function readParam(xml: string, id: string): string | undefined {
  for (const block of xml.match(PARAM_BLOCK) ?? []) {
    if (tagText(block, "ParamIdentifier") === id) {
      const v = tagText(block, "ParamValue");
      return v === null ? undefined : v;
    }
  }
  return undefined;
}

export interface PatchResult {
  xml: string;
  applied: string[];
  skipped: string[];
}

/** Format a float the way .epr files store them ("15." for integers). */
export function eprFloat(v: number): string {
  return Number.isInteger(v) ? `${v}.` : String(Number(v.toFixed(3)));
}

/** Replace the ParamValue of every ExporterParam with this identifier. */
export function setParam(xml: string, id: string, value: string): { xml: string; ok: boolean } {
  let ok = false;
  const out = xml.replace(PARAM_BLOCK, (block) => {
    if (tagText(block, "ParamIdentifier") !== id) return block;
    if (!/<ParamValue>[\s\S]*?<\/ParamValue>/.test(block)) return block;
    ok = true;
    return block.replace(/<ParamValue>[\s\S]*?<\/ParamValue>/, `<ParamValue>${escapeXml(value)}</ParamValue>`);
  });
  return { xml: out, ok };
}

/** Declared type of a parameter: 2 = integer, 3 = float, 4 = ticks (observed), or undefined. */
export function paramType(xml: string, id: string): number | undefined {
  for (const block of xml.match(PARAM_BLOCK) ?? []) {
    if (tagText(block, "ParamIdentifier") === id) {
      const t = Number(tagText(block, "ParamType"));
      return Number.isFinite(t) ? t : undefined;
    }
  }
  return undefined;
}

/** Write a number in the parameter's own format: integer for type 2, float ("15.") for type 3. */
export function setNumberParam(xml: string, id: string, value: number): { xml: string; ok: boolean } {
  const t = paramType(xml, id);
  const text = t === 3 ? eprFloat(value) : String(Math.round(value));
  return setParam(xml, id, text);
}

function setTag(xml: string, tag: string, value: string): { xml: string; ok: boolean } {
  const re = new RegExp(`<${tag}>[\\s\\S]*?</${tag}>`);
  if (!re.test(xml)) return { xml, ok: false };
  return { xml: xml.replace(re, `<${tag}>${escapeXml(value)}</${tag}>`), ok: true };
}

export interface PresetChanges {
  /** Shown in Media Encoder's preset name field. */
  name: string;
  /** New unique preset ID (UUID). */
  presetId: string;
  width?: number;
  height?: number;
  /** Sequence ticks per frame (exact), e.g. 10594584000 for 23.976. */
  fpsTicks?: string;
  /** Target video bitrate in Mbps. */
  bitrateMbps?: number;
  audio?: boolean;
  audioBitrateKbps?: number;
  audioSampleRate?: number;
}

/** Apply changes to a copy of a template preset. Only existing parameters are changed. */
export function patchPreset(template: string, c: PresetChanges): PatchResult {
  let xml = template;
  const applied: string[] = [];
  const skipped: string[] = [];
  const apply = (label: string, r: { xml: string; ok: boolean }) => {
    xml = r.xml;
    (r.ok ? applied : skipped).push(label);
  };

  apply("name", setTag(xml, "PresetName", c.name));
  apply("preset id", setTag(xml, "PresetID", c.presetId));
  if (c.width !== undefined) apply(`width ${c.width}`, setNumberParam(xml, "ADBEVideoWidth", c.width));
  if (c.height !== undefined) apply(`height ${c.height}`, setNumberParam(xml, "ADBEVideoHeight", c.height));
  if (c.fpsTicks !== undefined) apply("frame rate", setParam(xml, "ADBEVideoFPS", c.fpsTicks));
  if (c.bitrateMbps !== undefined) {
    apply(`bitrate ${c.bitrateMbps} Mbps`, setNumberParam(xml, "ADBEVideoTargetBitrate", c.bitrateMbps));
    // A maximum below the target would be invalid for VBR: raise it if needed.
    const max = Number(readParam(xml, "ADBEVideoMaxBitrate"));
    if (Number.isFinite(max) && max < c.bitrateMbps * 1.25) {
      apply("max bitrate", setNumberParam(xml, "ADBEVideoMaxBitrate", Math.round(c.bitrateMbps * 1.25 * 10) / 10));
    }
  }
  if (c.audio !== undefined) apply(c.audio ? "audio on" : "audio off", setTag(xml, "DoAudio", c.audio ? "true" : "false"));
  if (c.audio !== false) {
    if (c.audioBitrateKbps !== undefined) {
      // Only when the template stores a plain kbps value (AAC presets).
      const cur = Number(readParam(xml, "ADBEAudioBitrate"));
      if (Number.isFinite(cur) && cur >= 32 && cur <= 1536) {
        apply(`audio ${c.audioBitrateKbps} kbps`, setNumberParam(xml, "ADBEAudioBitrate", c.audioBitrateKbps));
      } else {
        skipped.push(`audio ${c.audioBitrateKbps} kbps`);
      }
    }
    if (c.audioSampleRate !== undefined) {
      const cur = Number(readParam(xml, "ADBEAudioRatePerSecond"));
      if (Number.isFinite(cur) && cur >= 8000) {
        apply(`${c.audioSampleRate / 1000} kHz`, setNumberParam(xml, "ADBEAudioRatePerSecond", c.audioSampleRate));
      } else {
        skipped.push(`${c.audioSampleRate / 1000} kHz`);
      }
    }
  }
  return { xml, applied, skipped };
}

/** RFC 4122 v4 UUID without crypto APIs (not all are available in UXP). */
export function makeUuid(random: () => number = Math.random): string {
  const h = Array.from({ length: 32 }, () => Math.floor(random() * 16));
  h[12] = 4;
  h[16] = (h[16] & 0x3) | 0x8;
  const s = h.map((n) => n.toString(16)).join("");
  return `${s.slice(0, 8)}-${s.slice(8, 12)}-${s.slice(12, 16)}-${s.slice(16, 20)}-${s.slice(20)}`;
}
