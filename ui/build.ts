import { build } from "bun";
import { readFileSync, writeFileSync, mkdirSync } from "fs";

try { mkdirSync("../www", { recursive: true }); } catch (_) {}

const result = await build({
  entrypoints: ["./src/index.tsx"],
  target: "browser",
  minify: true,
  define: {
    "process.env.NODE_ENV": '"production"',
  },
});

if (!result.success) {
  for (const msg of result.logs) console.error(msg);
  process.exit(1);
}

const bundle = await result.outputs[0].text();
const template = readFileSync("template.html", "utf-8");
const output = template.replace("<!-- BUNDLE_PLACEHOLDER -->", `<script>${bundle}</script>`);
writeFileSync("../www/index.html", output);
console.log(`Built: www/index.html (${(output.length / 1024).toFixed(1)} KB)`);
