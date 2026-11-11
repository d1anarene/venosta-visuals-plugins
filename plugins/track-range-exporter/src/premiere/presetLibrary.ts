/**
 * Finds Media Encoder / Premiere presets installed on this computer and writes adjusted copies
 * for the built-in export destinations. Needs the manifest permission localFileSystem:fullAccess
 * (Adobe's preset folders are outside the plugin's sandbox).
 *
 * Searched (read-only):
 *   - Premiere's own install:  <app>/Contents/MediaIO/systempresets (macOS),
 *                              <install dir>\MediaIO\systempresets (Windows)
 *   - Media Encoder installs next to Premiere ("Adobe Media Encoder <year>"), same sub-path
 *   - The user's saved Media Encoder presets: Documents/Adobe/Adobe Media Encoder/<ver>/Presets
 * Generated presets are written to the plugin's own data folder.
 */
import { makeUuid, patchPreset, readEprInfo, type PresetChanges } from "../domain/eprPreset";
import type { CandidatePreset } from "../domain/exportTargets";
import { diag } from "../ui/diagnostics";

// eslint-disable-next-line @typescript-eslint/no-require-imports
declare const require: (id: string) => any;
/* eslint-disable @typescript-eslint/no-explicit-any */

export interface InstalledPreset extends CandidatePreset {
  /** "system" = shipped with Premiere/AME, "user" = saved by the user in Media Encoder. */
  source: "system" | "user";
}

const isWindows = () => {
  try {
    return require("os").platform() === "win32";
  } catch {
    return false;
  }
};

function toFileUrl(nativePath: string): string {
  let p = nativePath.replace(/\\/g, "/");
  if (!p.startsWith("/")) p = "/" + p; // C:/x -> /C:/x
  return "file:" + p;
}

function join(...parts: string[]): string {
  const sep = isWindows() ? "\\" : "/";
  return parts
    .map((p, i) => (i === 0 ? p.replace(/[\\/]+$/, "") : p.replace(/^[\\/]+|[\\/]+$/g, "")))
    .join(sep);
}

function dirname(p: string): string {
  const i = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"));
  return i > 0 ? p.slice(0, i) : p;
}

/** Open a file/folder by absolute path. Tries the plain file: URL and a percent-encoded one,
 *  because paths contain spaces ("Adobe Premiere Pro 2026") and the docs do not specify which
 *  form UXP expects. */
async function entryAt(nativePath: string): Promise<any | null> {
  const fs = require("uxp").storage.localFileSystem;
  const url = toFileUrl(nativePath);
  for (const candidate of [url, encodeURI(url)]) {
    try {
      const e = await fs.getEntryWithUrl(candidate);
      if (e) return e;
    } catch {
      /* try the next form */
    }
  }
  return null;
}

/** File handles found while searching, so presets can be read again without path lookups. */
const entriesByPath = new Map<string, any>();

async function children(folder: any): Promise<any[]> {
  try {
    return await folder.getEntries();
  } catch {
    return [];
  }
}

/** Candidate root folders that may contain .epr presets. */
async function presetRoots(): Promise<Array<{ path: string; source: "system" | "user" }>> {
  const roots: Array<{ path: string; source: "system" | "user" }> = [];
  const uxp = require("uxp");
  const appPath: string = uxp.host?.applicationPath ?? "";
  const win = isWindows();
  diag("debug", `Host application path: ${appPath || "(unknown)"}`);

  // Premiere itself
  if (appPath) {
    if (win) roots.push({ path: join(dirname(appPath), "MediaIO", "systempresets"), source: "system" });
    else roots.push({ path: join(appPath, "Contents", "MediaIO", "systempresets"), source: "system" });
  }
  // Media Encoder installs next to Premiere's install folder
  const appsDir = appPath ? (win ? dirname(dirname(appPath)) : dirname(dirname(appPath))) : win ? "C:\\Program Files\\Adobe" : "/Applications";
  const apps = await entryAt(appsDir);
  for (const e of apps ? await children(apps) : []) {
    if (!e.isFolder || !/^Adobe Media Encoder/i.test(e.name)) continue;
    if (win) roots.push({ path: join(e.nativePath, "MediaIO", "systempresets"), source: "system" });
    else roots.push({ path: join(e.nativePath, `${e.name}.app`, "Contents", "MediaIO", "systempresets"), source: "system" });
  }
  // The user's own Media Encoder presets
  try {
    const home: string = require("os").homedir();
    const ameDocs = await entryAt(join(home, "Documents", "Adobe", "Adobe Media Encoder"));
    for (const v of ameDocs ? await children(ameDocs) : []) {
      if (v.isFolder) roots.push({ path: join(v.nativePath, "Presets"), source: "user" });
    }
  } catch {
    /* no home folder access */
  }
  return roots;
}

let cache: InstalledPreset[] | null = null;

/** All readable .epr presets found (cached for the session; pass refresh=true to rescan). */
export async function findInstalledPresets(refresh = false): Promise<InstalledPreset[]> {
  if (cache && !refresh) return cache;
  const found: InstalledPreset[] = [];
  const seen = new Set<string>();
  let files = 0;
  const walk = async (folder: any, depth: number, source: "system" | "user") => {
    if (depth > 4 || files > 3000) return;
    for (const e of await children(folder)) {
      if (e.isFolder) {
        await walk(e, depth + 1, source);
      } else if (/\.epr$/i.test(e.name) && !seen.has(e.nativePath)) {
        seen.add(e.nativePath);
        entriesByPath.set(e.nativePath, e);
        files++;
        try {
          const info = readEprInfo(String(await e.read()));
          if (info) found.push({ path: e.nativePath, name: info.name || e.name.replace(/\.epr$/i, ""), fileType: info.fileType, source });
        } catch {
          /* unreadable preset: skip */
        }
      }
    }
  };
  for (const r of await presetRoots()) {
    const folder = await entryAt(r.path);
    diag("debug", `Preset folder ${r.path}: ${folder ? "found" : "not found"}`);
    if (folder) await walk(folder, 0, r.source);
  }
  const byType = new Map<string, number>();
  for (const p of found) byType.set(p.fileType || "?", (byType.get(p.fileType || "?") ?? 0) + 1);
  diag("debug", `Installed presets: ${found.length} (${[...byType].map(([k, v]) => `${k}:${v}`).join(", ")})`);
  cache = found;
  return found;
}

/** Write an adjusted copy of `templatePath` to the plugin data folder; returns its native path. */
export async function writeAdjustedPreset(
  templatePath: string,
  fileName: string,
  changes: Omit<PresetChanges, "presetId">,
): Promise<{ path: string; applied: string[]; skipped: string[] }> {
  const fs = require("uxp").storage.localFileSystem;
  const tplEntry = entriesByPath.get(templatePath) ?? (await entryAt(templatePath));
  if (!tplEntry) throw new Error(`Template preset not readable: ${templatePath}`);
  const template = String(await tplEntry.read());
  const r = patchPreset(template, { ...changes, presetId: makeUuid() });
  const data = await fs.getDataFolder();
  let folder: any;
  try {
    folder = await data.getEntry("presets");
  } catch {
    folder = await data.createFolder("presets");
  }
  const file = await folder.createFile(fileName, { overwrite: true });
  await file.write(r.xml);
  diag("debug", `Preset written ${file.nativePath}; applied: ${r.applied.join(", ")}; skipped: ${r.skipped.join(", ") || "none"}`);
  return { path: file.nativePath, applied: r.applied, skipped: r.skipped };
}
