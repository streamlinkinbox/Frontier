/*============================================================================================================================================
                                                   SELECTIONINSPECTORVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Reproduce Inspector selection operations, accessible switches, mask snapshots and retained artwork behaviour.

import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { writeFileSync } from "node:fs";

const TextureDirectory = fileURLToPath(new URL("../../../Experimental/TextureStudio/", import.meta.url));
const PlaywrightExecutable = `${TextureDirectory}node_modules/playwright/cli.js`;
const Specifications = ["artwork-selection-inspector", "artwork-mask-authoring", "artwork-masks", "pattern-colour-sources", "stamp-stack-appearance", "stamp-vector-geometry", "stamp-vector-studio", "stamp-transform-studio"];
const BrowserExecution = spawnSync(process.execPath, [PlaywrightExecutable, "test", ...Specifications.map(Name => `tests/${Name}.spec.js`), "--global-timeout=420000", "--reporter=line"], { cwd: TextureDirectory, env: process.env, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
const Transcript = (BrowserExecution.stdout || "") + (BrowserExecution.stderr || "") + (BrowserExecution.error ? `\n${BrowserExecution.error.message}\n` : "");
writeFileSync(fileURLToPath(new URL("../Results/SelectionInspectorReproduction.log", import.meta.url)), Transcript);
process.stdout.write(Transcript);
process.exit(BrowserExecution.status ?? 1);
