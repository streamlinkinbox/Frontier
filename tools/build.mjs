// Bundles the app into a single self-contained HTML file: RoadGenerator/dist/road-generator.html
import { build } from "esbuild";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const app = path.join(root, "RoadGenerator");
const out = path.join(app, "dist");
fs.mkdirSync(out, { recursive: true });
const js = await build({ entryPoints: [path.join(app, "src/ui/app.js")], bundle: true, format: "iife", minify: true, write: false, target: ["es2022"], legalComments: "none" });
const css = fs.readFileSync(path.join(app, "src/ui/style.css"), "utf8");
let html = fs.readFileSync(path.join(app, "index.html"), "utf8");
html = html.replace('<link rel="stylesheet" href="./src/ui/style.css" />', `<style>${css}</style>`);
html = html.replace('<script type="module" src="./src/ui/app.js"></script>', `<script type="module">${js.outputFiles[0].text.replace(/<\/script/g, "<\\/script")}</script>`);
fs.writeFileSync(path.join(out, "road-generator.html"), html);
console.log(`built ${path.relative(root, path.join(out, "road-generator.html"))} (${(html.length / 1024).toFixed(0)} kB)`);
