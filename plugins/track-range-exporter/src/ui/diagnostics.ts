/**
 * In-panel diagnostic log for support. Keeps the last 300 entries in memory only (nothing is
 * written to disk or sent anywhere); "Copy Log" puts it on the clipboard for the user to share.
 * Clip names and paths can appear in it, so the user decides what to send.
 */
// eslint-disable-next-line @typescript-eslint/no-require-imports
declare const require: (id: string) => any;

interface Entry {
  t: string;
  kind: string;
  msg: string;
}

const entries: Entry[] = [];
const MAX = 300;

export function diag(kind: "info" | "warn" | "error" | "debug", msg: string, err?: unknown): void {
  let text = msg;
  if (err !== undefined) {
    const e = err as { message?: string; stack?: string };
    text += ` | ${e?.message ?? String(err)}`;
    if (e?.stack) text += `\n${e.stack}`;
  }
  entries.push({ t: new Date().toISOString().slice(11, 23), kind, msg: text });
  if (entries.length > MAX) entries.shift();
}

function hostInfo(): string[] {
  const lines: string[] = [];
  try {
    const uxp = require("uxp");
    lines.push(`Host: ${uxp.host?.name ?? "?"} ${uxp.host?.version ?? "?"} (build ${uxp.host?.buildNumber ?? "?"})`);
    lines.push(`UXP: ${uxp.versions?.uxp ?? "?"}  Plugin: ${uxp.versions?.plugin ?? "?"}`);
    lines.push(`Locale: ${uxp.host?.uiLocale ?? "?"}`);
  } catch (e) {
    lines.push(`Host info unavailable: ${e}`);
  }
  try {
    const os = require("os");
    lines.push(`OS: ${os.platform?.() ?? "?"} ${os.release?.() ?? ""} ${os.arch?.() ?? ""}`);
  } catch {
    /* os module not available */
  }
  return lines;
}

export function diagnosticsText(): string {
  return [
    "Track Range Exporter diagnostic log",
    `Created: ${new Date().toISOString()}`,
    ...hostInfo(),
    "----",
    ...entries.map((e) => `${e.t} ${e.kind.toUpperCase().padEnd(5)} ${e.msg}`),
  ].join("\n");
}

export async function copyDiagnostics(): Promise<boolean> {
  try {
    await (navigator as unknown as { clipboard: { writeText(t: string): Promise<void> } }).clipboard.writeText(
      diagnosticsText(),
    );
    return true;
  } catch (e) {
    diag("error", "Copying the log to the clipboard failed", e);
    return false;
  }
}
