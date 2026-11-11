/**
 * Safe, deterministic output file naming.
 *
 * - Removes characters illegal on Windows/macOS (and control characters).
 * - Avoids Windows reserved device names (CON, NUL, COM1...).
 * - Limits length (in UTF-8 bytes, conservative for all common filesystems).
 * - Detects collisions case-insensitively (macOS APFS default and Windows NTFS are
 *   case-insensitive) and after Unicode NFC normalisation.
 * - Never overwrites: collisions get " (2)", " (3)"... suffixes.
 */

const ILLEGAL = /[<>:"/\\|?*\u0000-\u001f\u007f]/g;
const RESERVED = /^(con|prn|aux|nul|com[0-9]|lpt[0-9])$/i;
/** Leave room for an ordinal prefix, a collision suffix and an extension within 255 bytes. */
export const MAX_BASENAME_BYTES = 180;

/** UTF-8 byte length without TextEncoder (not available in Premiere's UXP runtime). */
export function utf8Length(s: string): number {
  let n = 0;
  for (const ch of s) {
    const cp = ch.codePointAt(0) ?? 0;
    n += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
  }
  return n;
}

/** Unicode NFC normalisation when the runtime supports it; otherwise the string unchanged. */
export function nfc(s: string): string {
  try {
    return typeof s.normalize === "function" ? s.normalize("NFC") : s;
  } catch {
    return s;
  }
}

function truncateUtf8(s: string, maxBytes: number): string {
  if (utf8Length(s) <= maxBytes) return s;
  let out = "";
  for (const ch of s) {
    // iterate by code point, never split surrogate pairs
    if (utf8Length(out + ch) > maxBytes) break;
    out += ch;
  }
  return out;
}

/** Turn a clip display name into a safe base name (no extension). Never returns "". */
export function cleanBaseName(raw: string): string {
  let s = nfc(raw ?? "").replace(ILLEGAL, "_");
  // Strip a media extension that the clip name may carry ("A.mov" -> "A").
  s = s.replace(/\.(mov|mp4|mxf|r3d|braw|ari|arx|dng|mkv|avi|m4v|mts|crm|wav|png|jpe?g|tiff?|exr|dpx|prproj)$/i, "");
  s = s.replace(/\s+/g, " ").trim();
  s = s.replace(/^[. ]+|[. ]+$/g, ""); // no leading/trailing dots/spaces (Windows)
  if (!s) s = "clip";
  if (RESERVED.test(s)) s = `${s}_`;
  return truncateUtf8(s, MAX_BASENAME_BYTES).replace(/[. ]+$/g, "") || "clip";
}

/** Validate a user-edited base name. Returns an error message or null. */
export function validateBaseName(name: string): string | null {
  if (!name || !name.trim()) return "File name is empty.";
  if (ILLEGAL.test(name)) {
    ILLEGAL.lastIndex = 0;
    return 'File name contains illegal characters (< > : " / \\ | ? *).';
  }
  ILLEGAL.lastIndex = 0;
  if (/[. ]$/.test(name)) return "File name may not end with a dot or space.";
  if (RESERVED.test(name)) return "File name is a reserved system name.";
  if (utf8Length(name) > 240) return "File name is too long.";
  return null;
}

/** Normalise an extension from the preset: ".MP4" / "mp4" -> "mp4". */
export function normaliseExtension(ext: string): string {
  const e = (ext ?? "").trim().replace(/^\.+/, "");
  if (!/^[A-Za-z0-9]{1,10}$/.test(e)) throw new Error(`Unexpected file extension "${ext}"`);
  return e;
}

/** Key used for collision checks on case-insensitive filesystems. */
export function collisionKey(fileName: string): string {
  return nfc(fileName).toLowerCase();
}

export interface NamedJob {
  id: string;
  baseName: string;
}

export interface ResolvedName {
  id: string;
  fileName: string; // base + "." + ext
  renamed: boolean; // true when a collision suffix was added
}

/**
 * Resolve final file names for jobs in order. `existingFiles` are names already present in the
 * output folder (any case). Earlier jobs win; later duplicates get " (2)", " (3)", ...
 */
export function resolveFileNames(
  jobs: readonly NamedJob[],
  extension: string,
  existingFiles: Iterable<string>,
): ResolvedName[] {
  const ext = normaliseExtension(extension);
  const taken = new Set<string>();
  for (const f of existingFiles) taken.add(collisionKey(f));
  return jobs.map((job) => {
    const base = job.baseName.trim() || "clip";
    let candidate = `${base}.${ext}`;
    let n = 1;
    while (taken.has(collisionKey(candidate))) {
      n += 1;
      candidate = `${base} (${n}).${ext}`;
    }
    taken.add(collisionKey(candidate));
    return { id: job.id, fileName: candidate, renamed: n > 1 };
  });
}

/** Join folder + file using the folder's own separator style. */
export function joinPath(folder: string, fileName: string): string {
  const sep = folder.includes("\\") && !folder.includes("/") ? "\\" : "/";
  return folder.endsWith(sep) ? folder + fileName : folder + sep + fileName;
}
