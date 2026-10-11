import { build } from "esbuild";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Result = await build({
  entryPoints: [path.join(Folder, "FracturePanel.js")],
  bundle: true,
  minify: true,
  format: "iife",
  write: false,
  target: "es2022",
});
const Script = Result.outputFiles[0].text.replaceAll("</script", "<\\/script");
// Extract the exact main-editor slider rules rather than maintaining a lookalike.
const EditorStyle = fs.readFileSync(
  path.join(Folder, "../ProjectZeroEditor/Editor.css"),
  "utf8",
);
const SharedSelectors = new Set([
  'input[type="range"]',
  'input[type="range"]::-webkit-slider-thumb',
  'input[type="range"]::-moz-range-thumb',
  'input[type="range"]:disabled',
  ".field",
  ".field > span",
  ".field > select",
  ".slider-pill",
  ".split-value",
  ".split-value input",
  ".split-value small",
  ".slider-pill > input",
]);
const SharedStyle = [...EditorStyle.matchAll(/([^{}]+)\{([^{}]*)\}/g)]
  .filter((Match) => SharedSelectors.has(Match[1].trim()))
  .map((Match) => Match[0])
  .join("\n");
if (
  [...SharedSelectors].some(
    (Selector) => !SharedStyle.includes(Selector + " {"),
  )
)
  throw new Error("Missing main-editor slider styles");
const Fonts = ["Light", "Regular"]
  .map(
    (Weight, Index) =>
      `@font-face{font-family:'DM Sans';font-style:normal;font-weight:${Index ? 400 : 300};font-display:swap;src:url(data:font/ttf;base64,${fs.readFileSync(path.join(Folder, "../../EngineContent/Fonts/SunReference", "DMSans-" + Weight + ".ttf")).toString("base64")}) format('truetype')}`,
  )
  .join("\n");
const FontNotice = fs
  .readFileSync(
    path.join(Folder, "../../EngineContent/Fonts/SunReference/OFL.txt"),
    "utf8",
  )
  .replaceAll("&", "&amp;")
  .replaceAll("<", "&lt;");
const Html = fs
  .readFileSync(path.join(Folder, "FracturePanel.html"), "utf8")
  .replace(
    "/* INLINE_STYLE */",
    Fonts +
      "\n" +
      SharedStyle +
      "\n" +
      fs.readFileSync(path.join(Folder, "FracturePanel.css"), "utf8"),
  )
  .replace("/* INLINE_SCRIPT */", () => Script)
  .replace(
    "</body>",
    () => `<template id="font-license">${FontNotice}</template></body>`,
  );
fs.writeFileSync(path.join(Folder, "index.html"), Html);
console.log(
  `Built standalone fracture editor: ${(Buffer.byteLength(Html) / 1048576).toFixed(2)} MiB`,
);
