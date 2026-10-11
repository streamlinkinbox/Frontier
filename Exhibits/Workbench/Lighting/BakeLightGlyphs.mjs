//============================================================================================================================================
//                                                        BAKELIGHTGLYPHS.MJS
//============================================================================================================================================
// 📦 Rasterises the seven light header glyphs LightSpecification.js names, so the proof harness needs no SVG decoder of its own.

// LightPanel.js prepends a 25 x 25 img into every card header. The shipped editor rasterises those SVGs
//    through IconArt / ThorVG at runtime; the proof harness cannot, so the same artwork is baked here to
//    straight-alpha RGBA8 at 2x and read back with a plain fread.
import { Resvg } from "@resvg/resvg-js";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const Folder = path.dirname(fileURLToPath(import.meta.url));
const Frontier = path.resolve(Folder, "../../..");
const Edge = 50;

// LightSpecification.js LightIcons — reuse the shipped engine icon library, do not author a second set.
const LightIcons = {
  pointlight: "editor-point-light",
  spotlight: "editor-spotlight",
  ieslight: "editor-dome-light",
  arealight: "editor-area-light",
  tubelight: "light-area-2d",
  ledlight: "light-point-2d",
  ledstrip: "slate-ring-light",
};

fs.mkdirSync(path.join(Folder, "Glyphs"), { recursive: true });
for (const File of new Set(Object.values(LightIcons))) {
  const Source = fs.readFileSync(path.join(Frontier, "EngineContent/Icons", File + ".svg"));
  const Raster = new Resvg(Source, { fitTo: { mode: "width", value: Edge } }).render();
  const Pixels = Raster.asPng();
  // asPng gives an encoded stream; pixels() hands back the straight-alpha RGBA the harness wants.
  const Rgba = Buffer.from(Raster.pixels ?? Pixels);
  if (Rgba.length !== Edge * Edge * 4) {
    throw new Error(`${File}: expected ${Edge * Edge * 4} RGBA bytes, received ${Rgba.length}`);
  }
  fs.writeFileSync(path.join(Folder, "Glyphs", File + ".rgba"), Rgba);
  console.log(File, Rgba.length, "bytes");
}
