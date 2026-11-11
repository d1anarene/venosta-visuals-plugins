import { preflightManifest, runAmeQueue } from "../../src/export/ameAdapter";
import { ManifestError, parseManifest, type JobManifest } from "../../src/export/handoffManifest";
import { pickJobFile } from "../../src/ui/fileSystem";
import { jobStatus, realAmeHost, startQueue } from "./ameHost";

/** Enable/disable an element (Element.toggleAttribute is not available in UXP). */
function setDisabled(el: Element, disabled: boolean): void {
  if (disabled) el.setAttribute("disabled", "");
  else el.removeAttribute("disabled");
}

const $ = (id: string) => document.getElementById(id) as HTMLElement;
let manifest: JobManifest | null = null;
let cancel = false;
const hostIds = new Map<string, string>();
const statuses = new Map<string, string>();

function log(msg: string, kind: "info" | "error" | "warn" = "info") {
  const el = document.createElement("div");
  el.className = `msg msg-${kind}`;
  el.textContent = msg;
  $("messages").insertBefore(el, $("messages").firstChild);
}

function render() {
  const list = $("jobs");
  list.innerHTML = "";
  if (!manifest) return;
  $("info").textContent = `${manifest.sequenceName} · ${manifest.jobs.length} job(s)\n${manifest.projectPath}`;
  for (const j of manifest.jobs) {
    const row = document.createElement("div");
    row.className = "job";
    row.textContent = `${String(j.ordinal).padStart(3, "0")}  ${j.clipName}  (${j.expectedFrames} fr) — ${statuses.get(j.id) ?? "ready"}`;
    row.title = j.outputPath;
    list.appendChild(row);
  }
}

async function load() {
  const f = await pickJobFile();
  if (!f) return;
  try {
    manifest = parseManifest(f.text);
    statuses.clear();
    hostIds.clear();
    const problems = await preflightManifest(realAmeHost, manifest);
    for (const p of problems) log(p, p.startsWith("WARNING") ? "warn" : "error");
    const blocking = problems.filter((p) => !p.startsWith("WARNING"));
    setDisabled($("btn-enqueue"), blocking.length > 0);
    log(`Loaded ${manifest.jobs.length} job(s).`);
  } catch (e) {
    manifest = null;
    log(e instanceof ManifestError ? e.message : `Could not read the job file: ${e}`, "error");
  }
  render();
}

async function enqueue() {
  if (!manifest) return;
  cancel = false;
  $("btn-enqueue").setAttribute("disabled", "");
  const r = await runAmeQueue(realAmeHost, manifest, {
    isCancelled: () => cancel,
    onUpdate: (u) => {
      statuses.set(u.jobId, u.status + (u.message ? `: ${u.message}` : ""));
      if (u.hostJobId) hostIds.set(u.jobId, u.hostJobId);
      render();
    },
  });
  log(`${r.queued} queued, ${r.failed} failed, ${r.cancelled} cancelled. Check the ranges in the queue before rendering.`);
}

function poll() {
  if (manifest && hostIds.size) {
    let changed = false;
    for (const [id, hostId] of hostIds) {
      const s = jobStatus(hostId);
      if (s && statuses.get(id) !== s) {
        statuses.set(id, s);
        changed = true;
      }
    }
    if (changed) render();
  }
}

function init() {
  $("btn-load").addEventListener("click", () => void load());
  $("btn-enqueue").addEventListener("click", () => void enqueue());
  $("btn-cancel").addEventListener("click", () => (cancel = true));
  $("btn-start").addEventListener("click", () => void startQueue().then(() => log("Queue started.")).catch((e) => log(String(e), "error")));
  setInterval(poll, 2000);
}

if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
else init();
