// Bundles the Conductor panel into a ready-to-load UXP plugin folder (dist/panel).
import { build } from "esbuild";
import { cpSync, mkdirSync, rmSync } from "node:fs";

const out = "dist/panel";
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
cpSync("public", out, { recursive: true });
await build({
  entryPoints: ["src/panel/main.ts"],
  bundle: true,
  outfile: `${out}/index.js`,
  format: "iife",
  platform: "neutral",
  target: "es2020",
  external: ["premierepro", "uxp"],
  sourcemap: "inline",
  logLevel: "warning",
});
console.log(`built ${out}`);
