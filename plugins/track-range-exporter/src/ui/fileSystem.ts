/** UXP file-system helpers (pickers, persistent folder access, listing, writing). */

// eslint-disable-next-line @typescript-eslint/no-require-imports
declare const require: (id: string) => any;

/* eslint-disable @typescript-eslint/no-explicit-any */
type Entry = any;

function lfs(): any {
  return require("uxp").storage.localFileSystem;
}

export interface PickedFolder {
  entry: Entry;
  nativePath: string;
  token: string | null;
}

export async function pickFolder(): Promise<PickedFolder | null> {
  const entry = await lfs().getFolder();
  if (!entry || !entry.isFolder) return null; // user cancelled
  let token: string | null = null;
  try {
    token = await lfs().createPersistentToken(entry);
  } catch {
    token = null;
  }
  return { entry, nativePath: entry.nativePath, token };
}

export async function folderFromToken(token: string): Promise<PickedFolder | null> {
  try {
    const entry = await lfs().getEntryForPersistentToken(token);
    if (!entry || !entry.isFolder) return null;
    return { entry, nativePath: entry.nativePath, token };
  } catch {
    return null;
  }
}

export async function pickPresetFile(): Promise<string | null> {
  const file = await lfs().getFileForOpening({ types: ["epr"] });
  if (!file || !file.isFile) return null;
  return file.nativePath as string;
}

export async function pickJobFile(): Promise<{ text: string; path: string } | null> {
  const file = await lfs().getFileForOpening({ types: ["json"] });
  if (!file || !file.isFile) return null;
  const text = await file.read();
  return { text: String(text), path: file.nativePath };
}

/** Names of entries in the folder (for collision checks). Throws if the folder is inaccessible. */
export async function listFolderNames(folder: Entry): Promise<string[]> {
  const entries: Entry[] = await folder.getEntries();
  return entries.map((e) => String(e.name));
}

export async function writeTextFile(folder: Entry, name: string, text: string): Promise<string> {
  const file = await folder.createFile(name, { overwrite: false });
  await file.write(text);
  return file.nativePath as string;
}

/** Best-effort existence check for an absolute path (used by the AME companion). */
export async function pathExists(nativePath: string): Promise<boolean> {
  try {
    const url = "file:" + (nativePath.startsWith("/") ? "" : "/") + nativePath.replace(/\\/g, "/");
    const entry = await lfs().getEntryWithUrl(url);
    return !!entry;
  } catch {
    return false;
  }
}
