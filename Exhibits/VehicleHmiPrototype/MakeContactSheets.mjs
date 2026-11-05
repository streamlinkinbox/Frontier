//============================================================================================================================================
//                                                        MAKECONTACTSHEETS.MJS
//============================================================================================================================================
// 🧩 Tiles the Garage pictures of RunGarageEvidence.sh into one sheet per plan, each tile enlarged 2x without smoothing and captioned with what the
//    view had drawn when it was read (the .json beside each picture).
//
//        node Exhibits/VehicleHmiPrototype/MakeContactSheets.mjs <folder of paint-*.png and scan-*.png>
//
//    Writes paint-ripple-sheet.png and scan-sweep-sheet.png into the same folder. @napi-rs/canvas comes from Experimental/VehicleHmiPrototype/Tools.

import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const Here   = path.dirname(fileURLToPath(import.meta.url));
const Tools  = path.resolve(Here, "..", "..", "Experimental", "VehicleHmiPrototype", "Tools");
const Canvas = createRequire(path.join(Tools, "package.json"))("@napi-rs/canvas");
const Folder = path.resolve(process.argv[2] || path.join(Here, "Images"));

const Enlarge = 2;        // [-]  each tile is drawn at this multiple of the picture
const Caption = 22;       // [px] the caption band under a tile
const Gap     = 6;        // [px] between tiles

// 📝 What each plan's caption reads from the picture's .json (GarageHooks.mjs: Ripple { Elapsed, Radius }, Scan { Seconds, Front }).
const Plans = [
    {
        Name: "paint-ripple", Columns: 5, Sheet: "paint-ripple-sheet.png",
        Describe: (Report, Index) => Index === 0 ? "before the drop"
                                   : Report.Ripple.Radius > 0 ? `ripple ${Report.Ripple.Elapsed.toFixed(2)} s  radius ${Report.Ripple.Radius.toFixed(2)} m`
                                                              : "ripple over: finish committed"
    },
    {
        Name: "scan-sweep", Columns: 3, Sheet: "scan-sweep-sheet.png",
        Describe: (Report) => `scan ${Report.Scan.Seconds.toFixed(2)} s  front ${Report.Scan.Front >= 0 ? "+" : "−"}${Math.abs(Report.Scan.Front).toFixed(2)} m`
    }
];

let Written = 0;
for (const Plan of Plans)
{
    const Names = fs.readdirSync(Folder).filter((Name) => Name.startsWith(Plan.Name + "-") && Name.endsWith(".png") && !Name.endsWith("sheet.png")).sort();
    if (Names.length === 0)
    {
        console.log(`${Plan.Name}: no pictures in ${Folder}`);
        continue;
    }
    const First   = await Canvas.loadImage(fs.readFileSync(path.join(Folder, Names[0])));
    const TileW   = First.width * Enlarge, TileH = First.height * Enlarge;
    const Rows    = Math.ceil(Names.length / Plan.Columns);
    const Sheet   = Canvas.createCanvas(Plan.Columns * TileW + (Plan.Columns + 1) * Gap, Rows * (TileH + Caption) + (Rows + 1) * Gap);
    const Context = Sheet.getContext("2d");
    Context.fillStyle = "#101418";
    Context.fillRect(0, 0, Sheet.width, Sheet.height);
    Context.imageSmoothingEnabled = false;
    Context.font = "13px sans-serif";
    for (const [Index, Name] of Names.entries())
    {
        const Image  = await Canvas.loadImage(fs.readFileSync(path.join(Folder, Name)));
        const Report = JSON.parse(fs.readFileSync(path.join(Folder, Name.replace(/\.png$/, ".json")), "utf8"));
        const Left   = Gap + (Index % Plan.Columns) * (TileW + Gap);
        const Top    = Gap + Math.floor(Index / Plan.Columns) * (TileH + Caption + Gap);
        Context.drawImage(Image, Left, Top, TileW, TileH);
        Context.fillStyle = "#c8d4c6";
        Context.fillText(Plan.Describe(Report, Index), Left + 4, Top + TileH + 16);
    }
    fs.writeFileSync(path.join(Folder, Plan.Sheet), Sheet.toBuffer("image/png"));
    console.log(`${Plan.Sheet}: ${Names.length} pictures, ${Sheet.width} x ${Sheet.height}`);
    ++Written;
}
process.exit(Written === Plans.length ? 0 : 1);
