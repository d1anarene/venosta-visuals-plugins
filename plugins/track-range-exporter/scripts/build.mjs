// Bundles the Premiere panel and the AME companion into ready-to-load UXP plugin folders.
import { build } from "esbuild";
import { cpSync, mkdirSync, rmSync } from "node:fs";

const targets = [
  { entry: "src/index.ts", pub: "public", out: "dist/premiere-panel" },
  { entry: "ame-companion/src/index.ts", pub: "ame-companion/public", out: "dist/ame-companion" },
];
for (const t of targets) {
  rmSync(t.out, { recursive: true, force: true });
  mkdirSync(t.out, { recursive: true });
  cpSync(t.pub, t.out, { recursive: true });
  await build({
    entryPoints: [t.entry],
    bundle: true,
    outfile: `${t.out}/index.js`,
    format: "iife",
    platform: "neutral",
    target: "es2020",
    external: ["premierepro", "mediaencoder", "uxp"],
    sourcemap: "inline",
    logLevel: "warning",
  });
  console.log(`built ${t.out}`);
}
