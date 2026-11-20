/**
 * Preview graph (canvas 2D, only lines and rectangles): band level of the music (grey), detected
 * hits (orange lines), the resulting parameter curve (blue) and the written keys (dots).
 */
import { envAt } from "../core/envelope";
import { TICKS_PER_SECOND } from "../core/keyframes";
import type { Analysis, ClipTiming, Plan } from "./plan";

export function drawPreview(canvas: HTMLCanvasElement, an: Analysis | null, p: Plan | null, c: ClipTiming | null,
                            outMin: number, outMax: number): void {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  const w = canvas.width, h = canvas.height;
  ctx.fillStyle = "#1b1b1b";
  ctx.fillRect(0, 0, w, h);
  if (!an || !c) return;
  const t0 = c.targetStart / TICKS_PER_SECOND, t1 = c.targetEnd / TICKS_PER_SECOND;
  const x = (t: number) => ((t - t0) / Math.max(1e-6, t1 - t0)) * w;
  const toSrc = (t: number) => (c.audioIn + (t * TICKS_PER_SECOND - c.audioStart)) / TICKS_PER_SECOND;
  // level (dB, -60..0) as filled bars
  ctx.fillStyle = "#4a4a4a";
  for (let px = 0; px < w; ++px) {
    const t = t0 + ((t1 - t0) * px) / w;
    const db = envAt(an.envelope, toSrc(t) - an.offset);
    const v = Math.max(0, Math.min(1, (db + 60) / 60));
    ctx.fillRect(px, h - v * h, 1, v * h);
  }
  if (!p) return;
  ctx.strokeStyle = "#e08a2e";
  ctx.lineWidth = 1;
  for (const ht of p.hitTimes) {
    ctx.beginPath();
    ctx.moveTo(x(ht), 0);
    ctx.lineTo(x(ht), h);
    ctx.stroke();
  }
  const lo = Math.min(outMin, outMax), hi = Math.max(outMin, outMax), span = hi - lo || 1;
  const y = (v: number) => h - 4 - ((v - lo) / span) * (h - 8);
  const keyToT = (ticks: number) => (ticks - c.targetIn + c.targetStart) / TICKS_PER_SECOND;  // media-time keys
  ctx.strokeStyle = "#3d8ee8";
  ctx.lineWidth = 2;
  ctx.beginPath();
  p.full.forEach((k, i) => (i ? ctx.lineTo(x(keyToT(k.ticks)), y(k.value)) : ctx.moveTo(x(keyToT(k.ticks)), y(k.value))));
  ctx.stroke();
  if (p.keys.length < w / 3) {
    ctx.fillStyle = "#ffffff";
    for (const k of p.keys) ctx.fillRect(x(keyToT(k.ticks)) - 1.5, y(k.value) - 1.5, 3, 3);
  }
}
