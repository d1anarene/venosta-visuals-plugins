/**
 * EQ-like band picker: the music's average spectrum on a logarithmic 20 Hz - 20 kHz axis, the
 * chosen band shaded, its edges draggable (drag inside the band to move it). Canvas 2D, only
 * rectangles and lines (labels are HTML below the canvas).
 */
import { Spectrum, spectrumAt } from "../core/spectrum";

export const F_MIN = 20, F_MAX = 20000;
const LOG = Math.log(F_MAX / F_MIN);
export const xOf = (f: number, w: number) => (Math.log(Math.max(F_MIN, Math.min(F_MAX, f)) / F_MIN) / LOG) * w;
export const fOf = (x: number, w: number) => F_MIN * Math.exp((Math.max(0, Math.min(w, x)) / w) * LOG);

export interface BandEdges {
  low: number;   // 0 = open (no high-pass)
  high: number;  // 0 = open (no low-pass)
}

export function drawEq(canvas: HTMLCanvasElement, s: Spectrum | null, band: BandEdges): void {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  const w = canvas.width, h = canvas.height;
  ctx.fillStyle = "#1b1b1b";
  ctx.fillRect(0, 0, w, h);
  const lo = band.low > 0 ? band.low : F_MIN, hi = band.high > 0 ? band.high : F_MAX;
  const x0 = xOf(lo, w), x1 = xOf(hi, w);
  ctx.fillStyle = "#24384f";
  ctx.fillRect(x0, 0, Math.max(1, x1 - x0), h);
  ctx.fillStyle = "#333";
  for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) ctx.fillRect(Math.round(xOf(f, w)), 0, 1, h);
  if (s) {
    // dB -90..0 -> bottom..top; per pixel the loudest bin it covers
    for (let px = 0; px < w; ++px) {
      const db = spectrumAt(s, fOf(px, w), fOf(px + 1, w));
      const v = Math.max(0, Math.min(1, (db + 90) / 90));
      const inside = px >= x0 && px <= x1;
      ctx.fillStyle = inside ? "#5aa0f0" : "#5a5a5a";
      ctx.fillRect(px, h - v * h, 1, v * h);
    }
  }
  ctx.fillStyle = "#e08a2e";
  if (band.low > 0) ctx.fillRect(Math.round(x0) - 1, 0, 3, h);
  if (band.high > 0) ctx.fillRect(Math.round(x1) - 1, 0, 3, h);
}

/** Pointer dragging on the EQ canvas; `get`/`set` read and write the band (Hz). */
export function attachEqDrag(canvas: HTMLCanvasElement, get: () => BandEdges, set: (b: BandEdges) => void): void {
  let mode: "low" | "high" | "move" | null = null;
  let startX = 0, startBand: BandEdges = { low: 0, high: 0 };
  const scale = () => (canvas.clientWidth > 0 ? canvas.width / canvas.clientWidth : 1);
  const posX = (e: MouseEvent) => {
    const r = canvas.getBoundingClientRect?.();
    const local = r && r.width > 0 ? e.clientX - r.left : e.offsetX;
    return local * scale();
  };
  const down = (e: MouseEvent) => {
    const x = posX(e), w = canvas.width, b = get();
    const xl = xOf(b.low > 0 ? b.low : F_MIN, w), xh = xOf(b.high > 0 ? b.high : F_MAX, w);
    if (Math.abs(x - xl) <= 10 && Math.abs(x - xl) <= Math.abs(x - xh)) mode = "low";
    else if (Math.abs(x - xh) <= 10) mode = "high";
    else if (x > xl && x < xh) mode = "move";
    else mode = x < xl ? "low" : "high";  // click outside: move the nearer edge there
    startX = x;
    startBand = { ...b };
    move(e);
  };
  const move = (e: MouseEvent) => {
    if (!mode) return;
    const x = posX(e), w = canvas.width;
    const round = (f: number) => (f < 100 ? Math.round(f) : f < 1000 ? Math.round(f / 5) * 5 : Math.round(f / 50) * 50);
    if (mode === "low") set({ low: round(Math.min(fOf(x, w), (get().high || F_MAX) * 0.9)), high: get().high });
    else if (mode === "high") set({ low: get().low, high: round(Math.max(fOf(x, w), (get().low || F_MIN) * 1.1)) });
    else {
      const k = Math.exp(((x - startX) / w) * LOG);
      const lo = (startBand.low || F_MIN) * k, hi = (startBand.high || F_MAX) * k;
      if (lo >= F_MIN && hi <= F_MAX) set({ low: startBand.low ? round(lo) : 0, high: startBand.high ? round(hi) : 0 });
    }
  };
  const up = () => {
    mode = null;
  };
  canvas.addEventListener("mousedown", down);
  canvas.addEventListener("mousemove", move);
  canvas.addEventListener("mouseup", up);
  canvas.addEventListener("mouseleave", up);
}
