/*============================================================================================================================================
                                                ARTWORKMASKAUTHORINGVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Reproduce HTML mask authoring, actual pixel export, compact controls and original vector artwork checks.

import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { writeFileSync } from "node:fs";

const TextureDirectory = fileURLToPath(new URL("../../../Experimental/TextureStudio/", import.meta.url));
const ResultDirectory = fileURLToPath(new URL("../Results/", import.meta.url));
const Specifications = ["artwork-mask-authoring", "artwork-masks", "stamp-stack-appearance", "stamp-vector-geometry", "stamp-vector-studio", "pattern-colour-sources"];
const PlaywrightExecutable = fileURLToPath(new URL("../../../Experimental/TextureStudio/node_modules/playwright/cli.js", import.meta.url));
const Execution = spawnSync(process.execPath, [PlaywrightExecutable, "test", ...Specifications.map(Name => `tests/${Name}.spec.js`), "--global-timeout=240000", "--reporter=line"], { cwd: TextureDirectory, env: process.env, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
const Transcript = (Execution.stdout || "") + (Execution.stderr || "") + (Execution.error ? `\n${Execution.error.message}\n` : "");
writeFileSync(`${ResultDirectory}ArtworkMaskAuthoringChecks.log`, Transcript);
process.stdout.write(Transcript);
process.exit(Execution.status ?? 1);
