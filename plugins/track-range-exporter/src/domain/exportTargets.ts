/**
 * Built-in export destinations. Each destination is based on one of Media Encoder's own presets
 * that ships with Premiere/AME (found on the user's computer), with resolution, frame rate,
 * bitrate and audio adjusted by the panel. Templates are matched by format code (from the .epr's
 * ExporterFileType) and by name, so translated preset names still match by format.
 */
import { KNOWN_RATES } from "./frameMath";

export interface TemplateMatch {
  /** Four-character file type from the .epr, e.g. "H264", "HEVC", "MooV". */
  fileTypes: string[];
  /** Preferred preset names (English defaults), tried in order. Case-insensitive "contains". */
  preferNames: string[];
  /** If true, a preset of the right file type is accepted even when no name matches. */
  anyOfType: boolean;
}

export interface ExportTarget {
  id: string;
  group: "Web & Social" | "TV & Master";
  label: string;
  template: TemplateMatch;
  /** Output size; undefined = keep the template's (usually matches the sequence). */
  width?: number;
  height?: number;
  /** Bitrate slider in Mbps; undefined = codec without a bitrate setting (ProRes, DNxHR). */
  bitrate?: { min: number; max: number; default: number; step: number };
  /** AAC audio bitrate options in kbps; undefined = uncompressed audio (PCM). */
  audioBitrates?: number[];
  defaultAudioBitrate?: number;
  note?: string;
}

const H264: TemplateMatch = {
  fileTypes: ["H264"],
  preferNames: ["Match Source - High bitrate", "High Quality 1080p HD", "YouTube 1080p Full HD"],
  anyOfType: true,
};
const AAC = [128, 192, 256, 320];

export const EXPORT_TARGETS: ExportTarget[] = [
  {
    id: "youtube-1080", group: "Web & Social", label: "YouTube 1080p (H.264)", template: H264,
    width: 1920, height: 1080, bitrate: { min: 8, max: 50, default: 16, step: 1 },
    audioBitrates: AAC, defaultAudioBitrate: 320,
  },
  {
    id: "youtube-4k", group: "Web & Social", label: "YouTube 4K (H.264)", template: H264,
    width: 3840, height: 2160, bitrate: { min: 20, max: 120, default: 45, step: 5 },
    audioBitrates: AAC, defaultAudioBitrate: 320,
  },
  {
    id: "vimeo-1080", group: "Web & Social", label: "Vimeo 1080p (H.264)", template: H264,
    width: 1920, height: 1080, bitrate: { min: 10, max: 60, default: 20, step: 1 },
    audioBitrates: AAC, defaultAudioBitrate: 320,
  },
  {
    id: "insta-reel", group: "Web & Social", label: "Instagram Reels / TikTok 9:16 (1080×1920)", template: H264,
    width: 1080, height: 1920, bitrate: { min: 5, max: 30, default: 12, step: 1 },
    audioBitrates: AAC, defaultAudioBitrate: 256,
    note: "Use a 9:16 sequence for full-frame output; a 16:9 sequence is letterboxed.",
  },
  {
    id: "insta-feed", group: "Web & Social", label: "Instagram feed 4:5 (1080×1350)", template: H264,
    width: 1080, height: 1350, bitrate: { min: 5, max: 30, default: 10, step: 1 },
    audioBitrates: AAC, defaultAudioBitrate: 256,
    note: "Use a 4:5 sequence for full-frame output.",
  },
  {
    id: "insta-square", group: "Web & Social", label: "Square 1:1 (1080×1080)", template: H264,
    width: 1080, height: 1080, bitrate: { min: 5, max: 30, default: 10, step: 1 },
    audioBitrates: AAC, defaultAudioBitrate: 256,
  },
  {
    id: "h264-match", group: "Web & Social", label: "H.264 – sequence size", template: H264,
    bitrate: { min: 5, max: 150, default: 25, step: 1 }, audioBitrates: AAC, defaultAudioBitrate: 320,
  },
  {
    id: "hevc-match", group: "Web & Social", label: "H.265 / HEVC – sequence size",
    template: { fileTypes: ["HEVC"], preferNames: ["Match Source - High bitrate"], anyOfType: true },
    bitrate: { min: 5, max: 120, default: 20, step: 1 }, audioBitrates: AAC, defaultAudioBitrate: 320,
  },
  {
    id: "prores-422hq", group: "TV & Master", label: "Apple ProRes 422 HQ (TV / master)",
    template: { fileTypes: ["MooV"], preferNames: ["Apple ProRes 422 HQ"], anyOfType: false },
  },
  {
    id: "prores-422", group: "TV & Master", label: "Apple ProRes 422",
    template: { fileTypes: ["MooV"], preferNames: ["Apple ProRes 422"], anyOfType: false },
  },
  {
    id: "prores-4444", group: "TV & Master", label: "Apple ProRes 4444 (alpha / master)",
    template: { fileTypes: ["MooV"], preferNames: ["Apple ProRes 4444"], anyOfType: false },
  },
  {
    id: "dnxhr-hqx", group: "TV & Master", label: "DNxHR HQX (TV, 10-bit)",
    template: { fileTypes: ["MooV", "MXF ", "MXFf"], preferNames: ["DNxHR HQX"], anyOfType: false },
  },
];

export function findTarget(id: string): ExportTarget | undefined {
  return EXPORT_TARGETS.find((t) => t.id === id);
}

/** Frame-rate slider steps. Index 0 = keep the sequence's own rate. */
export const FPS_STEPS: Array<{ label: string; key: string | null }> = [
  { label: "Sequence", key: null },
  { label: "23.976", key: "23.976" },
  { label: "24", key: "24" },
  { label: "25", key: "25" },
  { label: "29.97", key: "29.97" },
  { label: "30", key: "30" },
  { label: "50", key: "50" },
  { label: "59.94", key: "59.94" },
  { label: "60", key: "60" },
];

/** Ticks per frame for a slider step; the sequence's own value for step 0. */
export function fpsTicksForStep(step: number, sequenceTicksPerFrame: string): string {
  const s = FPS_STEPS[Math.max(0, Math.min(FPS_STEPS.length - 1, Math.round(step)))];
  return s.key === null ? sequenceTicksPerFrame : KNOWN_RATES[s.key].toString();
}

export interface CandidatePreset {
  path: string;
  name: string;
  fileType: string;
}

/** Pick the best installed template for a destination, or null. */
export function chooseTemplate(target: ExportTarget, presets: readonly CandidatePreset[]): CandidatePreset | null {
  const ofType = presets.filter((p) => target.template.fileTypes.includes(p.fileType));
  for (const want of target.template.preferNames) {
    const w = want.toLowerCase();
    // exact name first, then "starts with" (so "Apple ProRes 422" does not pick "422 HQ" first)
    const exact = ofType.find((p) => p.name.toLowerCase() === w);
    if (exact) return exact;
    const starts = ofType.filter((p) => p.name.toLowerCase().startsWith(w));
    if (starts.length) return starts.sort((a, b) => a.name.length - b.name.length)[0];
  }
  return target.template.anyOfType && ofType.length ? ofType[0] : null;
}
