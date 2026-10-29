/*============================================================================================================================================
                                                   PATTERNCHANNELSGROUPVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Reproduce channel rasterization, nested/linked artwork edits and retained Inspector behaviour.

import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { writeFileSync } from "node:fs";

const TextureDirectory = fileURLToPath(new URL("../../../Experimental/TextureStudio/", import.meta.url));
const Specifications = ["pattern-channels-groups", "artwork-arrays", "artwork-selection-inspector", "artwork-mask-authoring", "artwork-masks", "pattern-colour-sources", "stamp-stack-appearance", "stamp-vector-geometry", "stamp-vector-studio", "stamp-transform-studio", "pattern-editor"];
// These two historical browser tests still target the superseded Pattern toolbar.
// Their current interactions are covered by the retained Inspector/array specifications.
const BrowserExecution = spawnSync(process.execPath, [`${TextureDirectory}node_modules/playwright/cli.js`, "test", ...Specifications.map(Caption => `tests/${Caption}.spec.js`), "--grep-invert=pointer resize/rotate|live preview uses", "--global-timeout=540000", "--reporter=line"], { cwd: TextureDirectory, env: process.env, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
const Transcript = (BrowserExecution.stdout || "") + (BrowserExecution.stderr || "") + (BrowserExecution.error ? `\n${BrowserExecution.error.message}\n` : "");
writeFileSync(fileURLToPath(new URL("../Results/PatternChannelsGroupReproduction.log", import.meta.url)), Transcript);
process.stdout.write(Transcript);
process.exit(BrowserExecution.status ?? 1);
