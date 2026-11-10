/**
 * Job file exchanged between the Premiere panel and the Media Encoder companion panel.
 *
 * UXP plugins in different Adobe apps cannot call each other directly, so the Premiere panel
 * writes this JSON file next to the outputs and the AME companion panel loads it. The file holds
 * only what AME needs: the SAVED project path, the sequence GUID, per-job tick ranges and output
 * paths. AME renders from the saved .prproj on disk, not from Premiere's live state.
 */
import { parseTicks } from "../domain/frameMath";

export const MANIFEST_FORMAT = "venosta.track-range-exporter.jobs";
export const MANIFEST_VERSION = 1;

export interface ManifestJob {
  id: string;
  ordinal: number;
  clipName: string;
  startTicks: string; // inclusive
  endTicks: string; // exclusive
  expectedFrames: number;
  outputPath: string;
}

export interface JobManifest {
  format: typeof MANIFEST_FORMAT;
  version: number;
  createdAt: string;
  projectPath: string;
  sequenceGuid: string;
  sequenceName: string;
  presetPath: string;
  ticksPerFrame: string;
  /** How endTicks must be passed to the host's out point. */
  outPointConvention: "exclusive" | "inclusive";
  jobs: ManifestJob[];
}

export function serializeManifest(m: JobManifest): string {
  return JSON.stringify(m, null, 2);
}

export class ManifestError extends Error {}

function req<T>(cond: T, msg: string): asserts cond {
  if (!cond) throw new ManifestError(msg);
}

/** Strictly validate an untrusted JSON string. Throws ManifestError with a readable message. */
export function parseManifest(text: string): JobManifest {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    throw new ManifestError("The job file is not valid JSON.");
  }
  const m = raw as Record<string, unknown>;
  req(m && typeof m === "object", "The job file is empty.");
  req(m.format === MANIFEST_FORMAT, "This is not a Track Range Exporter job file.");
  req(
    typeof m.version === "number" && m.version <= MANIFEST_VERSION,
    `Job file version ${String(m.version)} is newer than this panel supports. Update the AME companion.`,
  );
  for (const k of ["projectPath", "sequenceGuid", "presetPath", "ticksPerFrame"] as const) {
    req(typeof m[k] === "string" && (m[k] as string).length > 0, `Job file is missing "${k}".`);
  }
  req(/\.prproj$/i.test(m.projectPath as string), "projectPath must point to a saved .prproj file.");
  req(m.outPointConvention === "exclusive" || m.outPointConvention === "inclusive", "Bad outPointConvention.");
  const tpf = parseTicks(m.ticksPerFrame as string);
  req(tpf > 0n, "ticksPerFrame must be positive.");
  req(Array.isArray(m.jobs) && m.jobs.length > 0, "The job file contains no jobs.");
  const outputs = new Set<string>();
  const jobs = (m.jobs as unknown[]).map((j, i) => {
    const job = j as Record<string, unknown>;
    const where = `job ${i + 1}`;
    req(typeof job.id === "string", `${where}: missing id`);
    req(typeof job.outputPath === "string" && job.outputPath.length > 0, `${where}: missing outputPath`);
    let start: bigint, end: bigint;
    try {
      start = parseTicks(job.startTicks as string);
      end = parseTicks(job.endTicks as string);
    } catch {
      throw new ManifestError(`${where}: invalid tick range`);
    }
    req(end > start, `${where}: end must be after start`);
    const key = (job.outputPath as string).normalize("NFC").toLowerCase();
    req(!outputs.has(key), `${where}: duplicate output path ${String(job.outputPath)}`);
    outputs.add(key);
    return {
      id: job.id as string,
      ordinal: Number(job.ordinal ?? i + 1),
      clipName: String(job.clipName ?? ""),
      startTicks: start.toString(),
      endTicks: end.toString(),
      expectedFrames: Number(job.expectedFrames ?? 0),
      outputPath: job.outputPath as string,
    };
  });
  return {
    format: MANIFEST_FORMAT,
    version: m.version as number,
    createdAt: String(m.createdAt ?? ""),
    projectPath: m.projectPath as string,
    sequenceGuid: m.sequenceGuid as string,
    sequenceName: String(m.sequenceName ?? ""),
    presetPath: m.presetPath as string,
    ticksPerFrame: tpf.toString(),
    outPointConvention: m.outPointConvention as "exclusive" | "inclusive",
    jobs,
  };
}
