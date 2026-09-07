import { cp, readFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";

// GitHub Pages can publish /docs directly, without granting a GitHub App the
// separate workflows permission. This directory contains only generated output.
const source = fileURLToPath(new URL("../dist/", import.meta.url));
const destination = fileURLToPath(new URL("../docs/", import.meta.url));
const html = await readFile(`${source}/index.html`, "utf8");
if (!html.includes("/Frontier/assets/")) {
  throw new Error("Run the /Frontier/ Pages build before publishing docs.");
}
await rm(destination, { recursive: true, force: true });
await cp(source, destination, { recursive: true });
console.log(
  "Published the tested static build to docs/. Select this branch and /docs in GitHub Pages settings.",
);
