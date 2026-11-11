/**
 * Small preferences store (UXP localStorage). Stores only tool preferences — output folder token,
 * preset path, queue mode — never clip names, ranges or other project content.
 */
import type { OutPointConvention } from "../export/queueAdapter";

export type QueueMode = "direct" | "ame";

export interface Prefs {
  folderToken: string | null;
  folderPath: string | null;
  presetPath: string | null;
  mode: QueueMode;
  outPointConvention: OutPointConvention;
  includeDisabledClips: boolean;
  /** Built-in destination id, "custom" (own .epr file) or "user:<path>" (saved AME preset). */
  destination: string;
  fpsStep: number;
  /** Bitrate per destination id, Mbps. */
  bitrates: Record<string, number>;
  audio: boolean;
  audioKbps: number;
  audioRate: number;
}

const KEY = "venosta.track-range-exporter.prefs.v1";
const DEFAULTS: Prefs = {
  folderToken: null,
  folderPath: null,
  presetPath: null,
  mode: "direct",
  outPointConvention: "exclusive",
  includeDisabledClips: false,
  destination: "youtube-1080",
  fpsStep: 0,
  bitrates: {},
  audio: true,
  audioKbps: 320,
  audioRate: 48000,
};

export function loadPrefs(): Prefs {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return { ...DEFAULTS };
    const p = JSON.parse(raw) as Partial<Prefs>;
    return {
      ...DEFAULTS,
      ...p,
      mode: p.mode === "ame" ? "ame" : "direct",
      outPointConvention: p.outPointConvention === "inclusive" ? "inclusive" : "exclusive",
      bitrates: typeof p.bitrates === "object" && p.bitrates ? p.bitrates : {},
    };
  } catch {
    return { ...DEFAULTS };
  }
}

export function savePrefs(p: Prefs): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(p));
  } catch {
    /* storage unavailable: preferences simply are not remembered */
  }
}
