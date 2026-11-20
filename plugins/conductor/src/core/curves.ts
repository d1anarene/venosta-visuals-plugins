/**
 * Transfer / shape curves on 0..1 -> 0..1 (monotonic, f(0) = 0, f(1) = 1). Pure.
 */
export type Curve =
  | { kind: "linear" }
  | { kind: "easeIn" }       // quadratic
  | { kind: "easeOut" }
  | { kind: "easeInOut" }    // smoothstep-like cubic
  | { kind: "power"; exponent: number }  // > 1 slow start, < 1 fast start
  | { kind: "exp"; k: number }           // (e^(k u) - 1) / (e^k - 1); k > 0 slow start, k < 0 fast start
  | { kind: "bezier"; x1: number; y1: number; x2: number; y2: number };  // like CSS cubic-bezier

export const LINEAR: Curve = { kind: "linear" };

function bezier(c: { x1: number; y1: number; x2: number; y2: number }, u: number): number {
  const x1 = Math.min(1, Math.max(0, c.x1)), x2 = Math.min(1, Math.max(0, c.x2));
  const bx = (t: number) => 3 * x1 * t * (1 - t) ** 2 + 3 * x2 * t * t * (1 - t) + t ** 3;
  const by = (t: number) => 3 * c.y1 * t * (1 - t) ** 2 + 3 * c.y2 * t * t * (1 - t) + t ** 3;
  let lo = 0, hi = 1;
  for (let i = 0; i < 40; ++i) {  // bisection: deterministic, monotonic x
    const m = (lo + hi) / 2;
    if (bx(m) < u) lo = m;
    else hi = m;
  }
  return by((lo + hi) / 2);
}

export function applyCurve(c: Curve, u: number): number {
  const x = u <= 0 ? 0 : u >= 1 ? 1 : u;
  switch (c.kind) {
    case "linear": return x;
    case "easeIn": return x * x;
    case "easeOut": return 1 - (1 - x) * (1 - x);
    case "easeInOut": return x * x * (3 - 2 * x);
    case "power": return x ** Math.max(1e-3, c.exponent);
    case "exp": return Math.abs(c.k) < 1e-6 ? x : (Math.exp(c.k * x) - 1) / (Math.exp(c.k) - 1);
    case "bezier": return x === 0 || x === 1 ? x : bezier(c, x);
  }
}
