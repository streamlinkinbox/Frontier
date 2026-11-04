/*============================================================================================================================================
                                                       ARTWORKARRAYVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Reproduce editable arrays, unique captions and retained Inspector/artwork behaviour.

import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { writeFileSync } from "node:fs";

const TextureDirectory = fileURLToPath(new URL("../../../Experimental/TextureStudio/", import.meta.url));
const Specifications = ["artwork-arrays", "artwork-selection-inspector", "artwork-mask-authoring", "artwork-masks", "pattern-colour-sources", "stamp-stack-appearance", "stamp-vector-geometry", "stamp-vector-studio", "stamp-transform-studio"];
const BrowserExecution = spawnSync(process.execPath, [`${TextureDirectory}node_modules/playwright/cli.js`, "test", ...Specifications.map(Caption => `tests/${Caption}.spec.js`), "--global-timeout=420000", "--reporter=line"], { cwd: TextureDirectory, env: process.env, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
const Transcript = (BrowserExecution.stdout || "") + (BrowserExecution.stderr || "") + (BrowserExecution.error ? `\n${BrowserExecution.error.message}\n` : "");
writeFileSync(fileURLToPath(new URL("../Results/ArtworkArraysReproduction.log", import.meta.url)), Transcript);
process.stdout.write(Transcript);
process.exit(BrowserExecution.status ?? 1);
