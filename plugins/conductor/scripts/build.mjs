// Bundles the Conductor panel into a ready-to-load UXP plugin folder (dist/panel).
import { build } from "esbuild";
import { cpSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";

const out = "dist/panel";
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
cpSync("public", out, { recursive: true });
for (const [entry, name] of [["src/panel/main.ts", "index.js"], ["src/panel/mp3entry.ts", "mp3.js"]]) await build({
  entryPoints: [entry],
  bundle: true,
  outfile: `${out}/${name}`,
  format: "iife",
  platform: "neutral",
  target: "es2020",
  external: ["premierepro", "uxp"],
  mainFields: ["module", "main"],
  alias: { "@eshaz/web-worker": "./scripts/no-worker.js" },
  sourcemap: "inline",
  logLevel: "warning",
});
// The MP3 decoder embeds its WebAssembly as String.raw`...` literals full of control and
// non-ASCII characters (NUL bytes included). Premiere's UXP loader failed on that file, so each
// such literal becomes an ordinary ASCII-only string literal with the same value (the raw text).
for (const name of ["index.js", "mp3.js"]) {
const file = `${out}/${name}`;
let src = readFileSync(file, "utf8");
let res = "", i = 0, n = 0;
while (true) {
  const at = src.indexOf("String.raw`", i);
  if (at < 0) break;
  let j = at + 11;
  while (j < src.length && src[j] !== "`") j += src[j] === "\\" ? 2 : 1;
  const raw = src.slice(at + 11, j).replace(/\r\n?/g, "\n");  // template literals normalise line ends
  if (raw.includes("${")) throw new Error("String.raw with substitutions is not supported here");
  res += src.slice(i, at) + JSON.stringify(raw).replace(/[\u007f-\uffff]/g, (c) => "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0"));
  i = j + 1;
  ++n;
}
src = res + src.slice(i);
const bad = [...src].filter((c) => (c < " " && c !== "\n" && c !== "\r" && c !== "\t") || c > "~").length;
if (bad) throw new Error(`bundle still has ${bad} non-ASCII / control characters`);
writeFileSync(file, src);
console.log(`built ${file} (${n} raw literals made ASCII)`);
}
