/*============================================================================================================================================
                                                         STAMPVECTORVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Reproduce geometry, shared-canvas, decal export and editable preset validation with actual browser evidence.

import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { mkdirSync, writeFileSync } from "node:fs";

const Repository = fileURLToPath(new URL("../../../", import.meta.url));
const TextureStudio = resolve(Repository, "Experimental/TextureStudio");
const Results = resolve(Repository, "Exhibits/TextureStudio/Results/StampVectorChecks.json");
mkdirSync(resolve(Repository, "Exhibits/TextureStudio/Results"), { recursive: true });
const Evaluation = spawnSync(process.execPath, [
    resolve(TextureStudio, "node_modules/@playwright/test/cli.js"), "test",
    "tests/stamp-vector-geometry.spec.js", "tests/shape-editor-svg-geometry.spec.js",
    "tests/stamp-decal-model.spec.js", "tests/stamp-transform-model.spec.js",
    "tests/stamp-vector-studio.spec.js", "tests/stamp-stack-appearance.spec.js", "--reporter=json", "--global-timeout=180000",
], { cwd: TextureStudio, env: process.env, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
writeFileSync(Results, Evaluation.stdout || JSON.stringify({ error: Evaluation.error?.message, status: Evaluation.status }));
if (Evaluation.stderr) process.stderr.write(Evaluation.stderr);
console.log(`Validation exit: ${Evaluation.status}; results: ${Results}`);
process.exitCode = Evaluation.status ?? 1;
