/**
 * Premiere panel controller. Plain DOM + Spectrum UXP widgets (sp-button, sp-checkbox, sp-picker),
 * no framework. Layout is built from rows rather than <table> because UXP's table layout support
 * is limited.
 */
import { parseTicks, ticksToTimecode, type FrameRateInfo } from "../domain/frameMath";
import { joinPath, resolveFileNames, validateBaseName } from "../domain/naming";
import { buildPlan, rateOf, staleReason, totalFrames, videoTrackLabel } from "../domain/rangePlan";
import type { ExportJobPlan, ExportPlan, JobStatus } from "../domain/types";
import {
  MANIFEST_FORMAT,
  MANIFEST_VERSION,
  serializeManifest,
  type JobManifest,
} from "../export/handoffManifest";
import { runPremiereDirectQueue } from "../export/premiereEncoderAdapter";
import type { JobUpdate, QueueRequest } from "../export/queueAdapter";
import {
  getContext,
  getContextForSequence,
  listSequences,
  getPresetExtension,
  isAmeInstalled,
  launchEncoder,
  listVideoTracks,
  PremiereInOutHost,
  saveProject,
  scanTrack,
  startBatchEncode,
} from "../premiere/sequenceReader";
import { folderFromToken, listFolderNames, pickFolder, pickPresetFile, writeTextFile, type PickedFolder } from "./fileSystem";
import { copyDiagnostics, diag } from "./diagnostics";
import { loadPrefs, savePrefs, type Prefs } from "./settings";

interface Row {
  job: ExportJobPlan;
  include: boolean;
  baseName: string;
  status: JobStatus;
  message: string;
  fileName?: string;
}

const STATUS_LABEL: Record<JobStatus, string> = {
  scanned: "scanned",
  ready: "ready",
  adding: "adding…",
  queued: "queued",
  rendering: "rendering",
  done: "done",
  failed: "failed",
  cancelled: "cancelled",
  skipped: "skipped",
  unsupported: "unsupported",
};

/** Index of the selected sp-menu-item in a change event, or -1. */
function pickerIndex(e: Event): number {
  const picker = e.target as HTMLElement & { selectedIndex?: number };
  return typeof picker.selectedIndex === "number" ? picker.selectedIndex : -1;
}

/** Value of the selected sp-menu-item (UXP sp-picker reports selectedIndex reliably, not value). */
function pickerValue(e: Event): string {
  const picker = e.target as HTMLElement & { selectedIndex?: number; value?: string };
  const items = Array.from(picker.querySelectorAll("sp-menu-item"));
  const idx = picker.selectedIndex ?? -1;
  if (idx >= 0 && idx < items.length) return items[idx].getAttribute("value") ?? "";
  return String(picker.value ?? "");
}

/** Enable/disable an element (Element.toggleAttribute is not available in UXP). */
function setDisabled(el: Element, disabled: boolean): void {
  if (disabled) el.setAttribute("disabled", "");
  else el.removeAttribute("disabled");
}

const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;

class PanelController {
  private prefs: Prefs = loadPrefs();
  private folder: PickedFolder | null = null;
  private plan: ExportPlan | null = null;
  private rows: Row[] = [];
  private busy = false;
  private cancelRequested = false;
  private selectedTrack = 0;
  /** GUID of the sequence the user picked; null = follow Premiere's active sequence. */
  private pickedSequence: string | null = null;
  private sequenceGuids: string[] = [];
  private pollTimer: ReturnType<typeof setInterval> | null = null;

  async init(): Promise<void> {
    this.bind();
    if (this.prefs.folderToken) this.folder = await folderFromToken(this.prefs.folderToken);
    this.renderSettings();
    await this.refreshContext();
    // Keep the sequence list current (new/renamed sequences, active sequence changes) while idle.
    this.pollTimer = setInterval(() => {
      if (!this.busy) void this.refreshSequences(false);
    }, 3000);
  }

  // ---------- wiring ----------
  private bind(): void {
    $("btn-refresh-context").addEventListener("click", () => void this.refreshContext());
    $("btn-scan").addEventListener("click", () => void this.scan());
    $("btn-select-all").addEventListener("click", () => this.setAll(true));
    $("btn-select-none").addEventListener("click", () => this.setAll(false));
    $("btn-folder").addEventListener("click", () => void this.chooseFolder());
    $("btn-preset").addEventListener("click", () => void this.choosePreset());
    $("btn-queue").addEventListener("click", () => void this.addToQueue());
    $("btn-cancel").addEventListener("click", () => {
      this.cancelRequested = true;
      this.info("Cancelling after the current job…");
    });
    $("btn-start").addEventListener("click", () => void this.startQueue());
    $("btn-advanced").addEventListener("click", () => {
      const adv = $("advanced");
      const open = adv.classList.contains("hidden");
      if (open) adv.classList.remove("hidden");
      else adv.classList.add("hidden");
      $("btn-advanced").textContent = open ? "Advanced ▾" : "Advanced ▸";
    });
    $("btn-copy-log").addEventListener("click", async () => {
      const ok = await copyDiagnostics();
      if (ok) this.info("Log copied to the clipboard — paste it into a message.");
      else this.error("Could not copy the log. Please send a screenshot of the messages below instead.");
    });
    $("seq-picker").addEventListener("change", (e) => {
      const idx = pickerIndex(e);
      const guid = idx >= 0 ? this.sequenceGuids[idx] : null;
      if (!guid) return;
      this.pickedSequence = guid;
      this.selectedTrack = 0;
      this.clearPlan("");
      void this.refreshTracks();
    });
    $("track-picker").addEventListener("change", (e) => {
      const v = Number(pickerValue(e));
      if (Number.isInteger(v)) this.selectedTrack = v;
      this.clearPlan("Guide track changed — press Scan Track.");
    });
    $("mode-picker").addEventListener("change", (e) => {
      this.prefs.mode = pickerValue(e) === "ame" ? "ame" : "direct";
      savePrefs(this.prefs);
      this.renderSettings();
    });
    $("convention-picker").addEventListener("change", (e) => {
      this.prefs.outPointConvention = pickerValue(e) === "inclusive" ? "inclusive" : "exclusive";
      savePrefs(this.prefs);
    });
    $("chk-disabled").addEventListener("change", (e) => {
      this.prefs.includeDisabledClips = !!(e.target as HTMLInputElement).checked;
      savePrefs(this.prefs);
      this.clearPlan("Option changed — press Scan Track.");
    });
  }

  // ---------- messages ----------
  private info(msg: string): void {
    this.log(msg, "info");
  }
  private error(msg: string): void {
    this.log(msg, "error");
  }
  private log(msg: string, kind: "info" | "error" | "warn"): void {
    diag(kind, msg);
    const status = $("status");
    status.textContent = msg;
    status.className = `status status-${kind === "info" ? "ok" : kind}`;
    const el = document.createElement("div");
    el.className = `msg msg-${kind}`;
    el.textContent = msg;
    const box = $("messages");
    box.insertBefore(el, box.firstChild);
    while (box.childNodes.length > 30) box.removeChild(box.lastChild!);
  }

  // ---------- context ----------
  /** Refresh the sequence list and the tracks of the chosen sequence. */
  async refreshContext(): Promise<void> {
    await this.refreshSequences(true);
    await this.refreshTracks();
    $("ame-state").textContent = isAmeInstalled() ? "" : "Adobe Media Encoder was not found.";
  }

  /** The GUID scanning will use: the user's pick, else Premiere's active sequence. */
  private currentSequenceGuid(): string | null {
    return this.pickedSequence;
  }

  private async refreshSequences(reportErrors: boolean): Promise<void> {
    try {
      const seqs = await listSequences();
      if (this.pickedSequence && !seqs.some((q) => q.guid === this.pickedSequence)) this.pickedSequence = null;
      const shown = this.pickedSequence ?? seqs.find((q) => q.active)?.guid ?? seqs[0]?.guid ?? null;
      const key = seqs.map((q) => q.guid + q.name).join("|") + "#" + shown;
      if (key === this.lastSeqKey) {
        // Menu is current; still make sure the track list belongs to the shown sequence.
        if (shown && shown !== this.tracksFor) await this.refreshTracks();
        return;
      }
      this.lastSeqKey = key;
      diag("debug", `Sequences: ${seqs.map((q) => q.name + (q.active ? " (active)" : "")).join(", ") || "none"}`);
      const followedBefore = this.shownSequence;
      this.sequenceGuids = seqs.map((q) => q.guid);
      const picker = $("seq-picker");
      const menu = picker.querySelector("sp-menu") ?? picker;
      menu.innerHTML = "";
      seqs.forEach((q, i) => {
        const item = document.createElement("sp-menu-item");
        item.setAttribute("value", q.guid);
        item.textContent = q.name + (q.active ? "  (active)" : "");
        if (q.guid === shown) item.setAttribute("selected", "");
        menu.appendChild(item);
        if (q.guid === shown) (picker as HTMLElement & { selectedIndex?: number }).selectedIndex = i;
      });
      this.shownSequence = shown;
      if (seqs.length === 0 && reportErrors) this.error("This project has no sequences.");
      // Following the active sequence and it changed: drop the old scan.
      if (!this.pickedSequence && followedBefore !== null && followedBefore !== shown) {
        this.selectedTrack = 0;
        this.clearPlan("Active sequence changed — press Scan Track.");
      }
      if (shown && shown !== this.tracksFor) await this.refreshTracks();
    } catch (e) {
      if (reportErrors) this.error(String(e instanceof Error ? e.message : e));
    }
  }

  private lastSeqKey = "";
  private shownSequence: string | null = null;
  /** Sequence whose tracks are currently in the Guide track list. */
  private tracksFor: string | null = null;

  private async refreshTracks(): Promise<void> {
    try {
      const ctx = await getContext(this.pickedSequence ?? this.shownSequence);
      const tracks = await listVideoTracks(ctx.sequence);
      this.tracksFor = ctx.sequence.guid.toString();
      const picker = $("track-picker");
      const menu = picker.querySelector("sp-menu") ?? picker;
      menu.innerHTML = "";
      if (this.selectedTrack >= tracks.length) this.selectedTrack = 0;
      tracks.forEach((t) => {
        const item = document.createElement("sp-menu-item");
        item.setAttribute("value", String(t.index));
        item.textContent = `${videoTrackLabel(t.index)}${t.name ? " / " + t.name : ""}${t.muted ? " (hidden)" : ""}`;
        if (t.index === this.selectedTrack) item.setAttribute("selected", "");
        menu.appendChild(item);
      });
      (picker as HTMLElement & { selectedIndex?: number }).selectedIndex = this.selectedTrack;
      if (tracks.length === 0) this.error("This sequence has no video tracks.");
    } catch (e) {
      this.error(String(e instanceof Error ? e.message : e));
    }
  }

  private clearPlan(reason: string): void {
    this.plan = null;
    this.rows = [];
    this.renderRows();
    if (reason) this.info(reason);
  }

  // ---------- scan ----------
  async scan(): Promise<void> {
    if (this.busy) return;
    try {
      this.info("Scanning…");
      const ctx = await getContext(this.currentSequenceGuid() ?? this.shownSequence);
      const snap = await scanTrack(ctx, this.selectedTrack);
      const plan = buildPlan(snap, { includeDisabledClips: this.prefs.includeDisabledClips });
      this.plan = plan;
      this.rows = plan.jobs.map((job) => ({
        job,
        include: job.includeByDefault,
        baseName: job.defaultBaseName,
        status: job.skippable ? "skipped" : "scanned",
        message: job.warnings.map((w) => w.message).join(" "),
      }));
      for (const w of plan.trackWarnings) this.log(w.message, "warn");
      this.info(
        `${snap.sequenceName} / ${videoTrackLabel(snap.track.index)}: ${plan.jobs.length} clip(s) found` +
          (snap.transitionCount ? `, ${snap.transitionCount} transition(s) on the guide track.` : "."),
      );
      if (plan.jobs.length === 0) this.log("No clips on this track — nothing to export.", "warn");
      this.renderRows();
    } catch (e) {
      diag("error", "Scan failed", e);
      // Report first, then clean up: the message must appear even if cleanup fails.
      this.error(`Scan failed: ${e instanceof Error ? e.message : String(e)}`);
      try {
        this.plan = null;
        this.rows = [];
        this.renderRows();
      } catch (cleanupError) {
        diag("error", "Cleanup after scan failure failed", cleanupError);
      }
    }
  }

  private setAll(v: boolean): void {
    for (const r of this.rows) if (!r.job.skippable) r.include = v;
    this.renderRows();
  }

  // ---------- settings ----------
  private renderSettings(): void {
    $("folder-path").textContent = this.folder?.nativePath ?? "No folder chosen";
    $("preset-path").textContent = this.prefs.presetPath ?? "No preset chosen";
    const isAme = this.prefs.mode === "ame";
    $("btn-queue").textContent = isAme ? "Write Job File for Media Encoder" : "Add Selected to AME Queue";
    $("mode-help").textContent = isAme
      ? "Never touches your timeline. Saves the project and writes a job file; open the " +
        "'Track Range Exporter – AME' panel in Media Encoder (27.0+) and load it. AME renders the saved project."
      : "One click. Temporarily sets the sequence In/Out per job while queueing, then restores your " +
        "original In/Out. Not yet verified in a real Premiere host — run the spike test first.";
    setDisabled($("btn-start"), isAme || this.busy);
    ($("chk-disabled") as HTMLInputElement).checked = this.prefs.includeDisabledClips;
    this.selectPicker("mode-picker", this.prefs.mode);
    this.selectPicker("convention-picker", this.prefs.outPointConvention);
  }

  private selectPicker(id: string, value: string): void {
    const picker = $(id) as HTMLElement & { selectedIndex?: number };
    picker.querySelectorAll("sp-menu-item").forEach((el, i) => {
      if (el.getAttribute("value") === value) {
        el.setAttribute("selected", "");
        if (picker.selectedIndex !== i) picker.selectedIndex = i;
      } else {
        el.removeAttribute("selected");
      }
    });
  }

  private async chooseFolder(): Promise<void> {
    const f = await pickFolder();
    if (!f) {
      this.info("Folder selection cancelled.");
      return;
    }
    this.folder = f;
    this.prefs.folderToken = f.token;
    this.prefs.folderPath = f.nativePath;
    savePrefs(this.prefs);
    this.renderSettings();
  }

  private async choosePreset(): Promise<void> {
    const p = await pickPresetFile();
    if (!p) {
      this.info("Preset selection cancelled.");
      return;
    }
    this.prefs.presetPath = p;
    savePrefs(this.prefs);
    this.renderSettings();
  }

  // ---------- table ----------
  private renderRows(): void {
    const list = $("job-list");
    list.innerHTML = "";
    const rate: FrameRateInfo | null = this.plan ? rateOf(this.plan.snapshot) : null;
    const zero = this.plan ? parseTicks(this.plan.snapshot.zeroPointTicks) : 0n;
    for (const row of this.rows) {
      const el = document.createElement("div");
      el.className = `job job-${row.status}`;
      const chk = document.createElement("sp-checkbox") as HTMLInputElement;
      chk.setAttribute("aria-label", `Include job ${row.job.ordinal}`);
      if (row.include) chk.setAttribute("checked", "");
      if (row.job.skippable || this.busy) chk.setAttribute("disabled", "");
      chk.addEventListener("change", (e) => {
        row.include = !!(e.target as HTMLInputElement).checked;
        this.renderSummary();
      });

      const head = document.createElement("div");
      head.className = "job-head";
      const ord = document.createElement("span");
      ord.className = "ord";
      ord.textContent = String(row.job.ordinal).padStart(3, "0");
      const name = document.createElement("span");
      name.className = "clip";
      name.textContent = row.job.guideClipName;
      name.title = row.job.guideClipName;
      const status = document.createElement("span");
      status.className = "status-chip";
      status.textContent = STATUS_LABEL[row.status];
      head.append(chk, ord, name, status);

      const times = document.createElement("div");
      times.className = "times";
      if (rate) {
        const s = parseTicks(row.job.startTick);
        const e = parseTicks(row.job.endTick);
        times.textContent =
          `IN ${ticksToTimecode(s, rate, zero)}  OUT ${ticksToTimecode(e, rate, zero)}  ` +
          `(${row.job.expectedFrames} fr)`;
        times.title = "OUT is exclusive: the first frame after the clip.";
      }

      const nameField = document.createElement("input");
      nameField.type = "text";
      nameField.className = "outname";
      nameField.value = row.baseName;
      nameField.setAttribute("aria-label", `Output file name for job ${row.job.ordinal}`);
      if (this.busy) nameField.setAttribute("disabled", "");
      nameField.addEventListener("change", () => {
        const v = nameField.value.trim();
        const err = validateBaseName(v);
        if (err) {
          this.error(`Job ${row.job.ordinal}: ${err}`);
          nameField.value = row.baseName;
          return;
        }
        row.baseName = v;
      });

      el.append(head, times, nameField);
      if (row.fileName) {
        const f = document.createElement("div");
        f.className = "note";
        f.textContent = `→ ${row.fileName}`;
        el.appendChild(f);
      }
      if (row.message) {
        const m = document.createElement("div");
        m.className = row.status === "failed" ? "note note-error" : "note";
        m.textContent = row.message;
        el.appendChild(m);
      }
      list.appendChild(el);
    }
    this.renderSummary();
  }

  private renderSummary(): void {
    const sel = this.rows.filter((r) => r.include && !r.job.skippable);
    const queued = this.rows.filter((r) => r.status === "queued").length;
    let dur = "";
    if (this.plan && sel.length) {
      const rate = rateOf(this.plan.snapshot);
      const frames = BigInt(totalFrames(sel.map((r) => r.job)));
      dur = ` · ${ticksToTimecode(frames * rate.ticksPerFrame, rate)} total`;
    }
    $("summary").textContent = `${sel.length} selected / ${queued} queued${dur}`;
    setDisabled($("btn-queue"), this.busy || sel.length === 0);
    setDisabled($("btn-cancel"), !this.busy);
  }

  private setBusy(b: boolean): void {
    this.busy = b;
    for (const id of ["btn-scan", "btn-refresh-context", "btn-folder", "btn-preset", "btn-select-all", "btn-select-none"]) {
      setDisabled($(id), b);
    }
    this.renderSettings();
    this.renderRows();
  }

  private update(u: JobUpdate): void {
    const row = this.rows.find((r) => r.job.id === u.jobId);
    if (!row) return;
    row.status = u.status;
    row.message = u.message ?? (u.status === "queued" ? "" : row.message);
    this.renderRows();
  }

  // ---------- queueing ----------
  private async validate(): Promise<{ requests: QueueRequest[]; extension: string } | null> {
    if (!this.plan) {
      this.error("Scan a track first.");
      return null;
    }
    const selected = this.rows.filter((r) => r.include && !r.job.skippable);
    if (!selected.length) {
      this.error("No jobs selected.");
      return null;
    }
    if (!this.folder) {
      this.error("Choose an output folder.");
      return null;
    }
    const presetPath = this.prefs.presetPath;
    if (!presetPath) {
      this.error("Choose an AME export preset (.epr).");
      return null;
    }
    for (const r of selected) {
      const err = validateBaseName(r.baseName);
      if (err) {
        this.error(`Job ${r.job.ordinal}: ${err}`);
        return null;
      }
    }
    if (this.prefs.mode === "direct" && !isAmeInstalled()) {
      this.error("Adobe Media Encoder is not installed or not compatible with this Premiere version.");
      return null;
    }

    // Stale check: re-read the timeline and compare to the scan.
    const ctx = await getContextForSequence(this.plan.snapshot.sequenceGuid);
    const fresh = await scanTrack(ctx, this.plan.snapshot.track.index);
    const stale = staleReason(this.plan, fresh);
    if (stale) {
      this.error(`The timeline changed since scanning (${stale}). Press Scan Track again.`);
      return null;
    }

    let extension: string;
    try {
      extension = await getPresetExtension(ctx.sequence, presetPath);
      if (!extension) throw new Error("empty");
    } catch {
      this.error("Could not read the output format from the preset. Is the .epr file valid and present?");
      return null;
    }

    let existing: string[];
    try {
      existing = await listFolderNames(this.folder.entry);
    } catch {
      this.error("The output folder is not accessible. Choose it again.");
      return null;
    }
    const resolved = resolveFileNames(
      selected.map((r) => ({ id: r.job.id, baseName: r.baseName })),
      extension,
      existing,
    );
    const requests: QueueRequest[] = selected.map((r, i) => {
      r.fileName = resolved[i].fileName;
      r.status = "ready";
      r.message = resolved[i].renamed ? "Name already used — a number was added to avoid overwriting." : "";
      return { job: r.job, outputPath: joinPath(this.folder!.nativePath, resolved[i].fileName), presetPath };
    });
    this.renderRows();
    return { requests, extension };
  }

  async addToQueue(): Promise<void> {
    if (this.busy) return;
    this.cancelRequested = false;
    this.setBusy(true);
    try {
      const v = await this.validate();
      if (!v) return;
      if (this.prefs.mode === "ame") await this.writeJobFile(v.requests);
      else await this.queueDirect(v.requests);
    } catch (e) {
      this.error(String(e instanceof Error ? e.message : e));
    } finally {
      this.setBusy(false);
    }
  }

  private async queueDirect(requests: QueueRequest[]): Promise<void> {
    const plan = this.plan!;
    const ctx = await getContextForSequence(plan.snapshot.sequenceGuid);
    const host = new PremiereInOutHost(ctx);
    const result = await runPremiereDirectQueue(
      host,
      requests,
      parseTicks(plan.snapshot.ticksPerFrame),
      this.prefs.outPointConvention,
      {
        onUpdate: (u) => this.update(u),
        isCancelled: () => this.cancelRequested,
        verifyUnchanged: async () => {
          try {
            const c = await getContextForSequence(plan.snapshot.sequenceGuid);
            const fresh = await scanTrack(c, plan.snapshot.track.index);
            return staleReason(plan, fresh);
          } catch (e) {
            return String(e instanceof Error ? e.message : e);
          }
        },
      },
    );
    for (const f of result.fatalErrors) this.error(f);
    this.info(
      `${result.queued} job(s) added to Media Encoder` +
        (result.failed ? `, ${result.failed} failed` : "") +
        (result.cancelled ? `, ${result.cancelled} not queued` : "") +
        ". Your original In/Out was restored." +
        (result.queued ? " Check each job's range in AME before rendering." : ""),
    );
  }

  private async writeJobFile(requests: QueueRequest[]): Promise<void> {
    const plan = this.plan!;
    const ctx = await getContextForSequence(plan.snapshot.sequenceGuid);
    if (!ctx.project.path || !/\.prproj$/i.test(ctx.project.path)) {
      this.error("Save the project first (File > Save). Media Encoder renders the saved project file.");
      return;
    }
    // AME reads the saved project from disk, so save now to include the current grade.
    const saved = await saveProject(ctx.project);
    if (!saved) {
      this.error("Premiere could not save the project. Nothing was written.");
      return;
    }
    const manifest: JobManifest = {
      format: MANIFEST_FORMAT,
      version: MANIFEST_VERSION,
      createdAt: new Date().toISOString(),
      projectPath: ctx.project.path,
      sequenceGuid: plan.snapshot.sequenceGuid,
      sequenceName: plan.snapshot.sequenceName,
      presetPath: requests[0].presetPath,
      ticksPerFrame: plan.snapshot.ticksPerFrame,
      outPointConvention: this.prefs.outPointConvention,
      jobs: requests.map((r) => ({
        id: r.job.id,
        ordinal: r.job.ordinal,
        clipName: r.job.guideClipName,
        startTicks: r.job.startTick,
        endTicks: r.job.endTick,
        expectedFrames: r.job.expectedFrames,
        outputPath: r.outputPath,
      })),
    };
    const stamp = new Date().toISOString().replace(/[:.]/g, "-");
    const path = await writeTextFile(this.folder!.entry, `TrackRangeExporter_jobs_${stamp}.json`, serializeManifest(manifest));
    for (const r of requests) this.update({ jobId: r.job.id, status: "ready", message: "In job file — load it in the AME panel." });
    this.info(`Project saved and job file written: ${path}. In Media Encoder open Window > Extensions > Track Range Exporter – AME and load this file.`);
  }

  private async startQueue(): Promise<void> {
    try {
      await launchEncoder();
      const ok = await startBatchEncode();
      this.info(ok ? "Media Encoder queue started." : "Media Encoder did not confirm the start.");
    } catch (e) {
      this.error(`Could not start Media Encoder: ${e}`);
    }
  }
}

let controller: PanelController | null = null;

export function startPanel(): void {
  if (controller) return;
  controller = new PanelController();
  void controller.init();
}

/** Called when the panel is shown again (UXP entrypoint "show"): re-read sequences and tracks. */
export function onPanelShown(): void {
  if (controller) void controller.refreshContext();
}
