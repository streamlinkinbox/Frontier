import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const Folder = path.dirname(fileURLToPath(import.meta.url)),
  Require = createRequire(path.resolve(Folder, "package.json"));
const { build } = Require("esbuild");
// Reuse the exact embedded Frontier World Mirror icon/font distribution. No
// network fonts, substitute icon fonts or changes to the World Mirror build.
const Mirror = fs.readFileSync(
    path.join(Folder, "../FrontierWorldMirror/index.html"),
    "utf8",
  ),
  Marker = "window.NativeAssets=",
  Start = Mirror.indexOf(Marker) + Marker.length;
if (Start < Marker.length) throw new Error("Build FrontierWorldMirror first.");
let Depth = 0,
  Quoted = false,
  Escaped = false,
  End = Start;
for (; End < Mirror.length; End++) {
  const C = Mirror[End];
  if (Quoted) {
    if (Escaped) Escaped = false;
    else if (C === "\\") Escaped = true;
    else if (C === '"') Quoted = false;
  } else if (C === '"') Quoted = true;
  else if (C === "{") Depth++;
  else if (C === "}" && --Depth === 0) {
    End++;
    break;
  }
}
const Native = JSON.parse(Mirror.slice(Start, End));
const Names = [
  "package",
  "mesh",
  "material",
  "editor-prefab",
  "editor-material",
  "editor-texture",
  "editor-move",
  "editor-pivot",
  "editor-scale",
  "editor-rotate",
  "editor-bake",
  "editor-box-collider",
  "slate-build",
  "slate-document",
  "slate-animation-curve",
  "slate-video-player",
  "collection",
  "asset-folder-models",
  "asset-folder-textures",
  "asset-folder-materials",
  "asset-folder-audio",
  "asset-folder-scenes",
  ...Object.keys(Native.Icons).filter((K) => K.startsWith("asset-file-")),
];
const Assets = {
  Icons: Object.fromEntries(
    Names.map((K) => [K, Native.Icons[K]]).filter(([, V]) => V),
  ),
  Glyphs: Native.Glyphs,
};
const Fonts = Object.entries(Native.Fonts)
  .map(
    ([Weight, Url]) =>
      `@font-face{font-family:'DM Sans';font-style:normal;font-weight:${Weight === "Light" ? 300 : 400};font-display:swap;src:url('${Url}') format('truetype')}`,
  )
  .join("\n");
const Licenses = [
  "THREE.JS\n" +
    fs.readFileSync(path.join(Folder, "node_modules/three/LICENSE"), "utf8"),
  "REACT / REACT DOM\n" +
    fs.readFileSync(path.join(Folder, "node_modules/react/LICENSE"), "utf8"),
  "FRONTIER SHIPPED ICONS\n" +
    (Native.Notices?.Icons || "Repository-shipped Frontier icon distribution."),
  "DM SANS\n" +
    (Native.Notices?.Fonts || "See FrontierWorldMirror font notices."),
].join("\n\n");
fs.writeFileSync(path.join(Folder, "LICENSES.txt"), Licenses);
const Output = await build({
  entryPoints: [path.join(Folder, "ImportDialogue.jsx")],
  bundle: true,
  write: false,
  minify: true,
  format: "iife",
  outfile: "ImportDialogue.js",
  nodePaths: [
    path.join(Folder, "node_modules"),
    path.join(Folder, "../FrontierEditor/node_modules"),
  ],
  define: { "process.env.NODE_ENV": '"production"' },
  target: ["chrome110", "firefox115", "safari16"],
});
const Script = Output.outputFiles.find((F) => F.path.endsWith(".js")).text,
  Style = Output.outputFiles.find((F) => F.path.endsWith(".css")).text;
fs.writeFileSync(
  path.join(Folder, "index.html"),
  `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark"><meta name="description" content="Frontier import inspector — standalone file review, geometry, UVs, PBR materials, distance fields, clustered LOD baking and metadata reference."><title>Frontier · Import inspector</title><style>${Fonts}\n${Style}</style></head><body><div id="ImportDemo"></div><script>window.NativeAssets=${JSON.stringify(Assets).replaceAll("<", "\\u003c")};</script><script>${Script.replace(/[ \t]+$/gm, "").replaceAll("</script", "<\\/script")}</script><script type="text/plain" id="ThirdPartyLicenses">${Licenses.replaceAll("<", "&lt;")}</script></body></html>`,
);
console.log(
  `Built ImportDialogue/index.html (${(fs.statSync(path.join(Folder, "index.html")).size / 1048576).toFixed(2)} MiB), fully self-contained.`,
);
