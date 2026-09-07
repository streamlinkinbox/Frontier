import { cp, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { checkPages } from "./check-pages.mjs";

// Pages can publish /docs without granting a GitHub App workflow permission.
// Validate BEFORE replacing the last committed, working deployment artifact.
const source = fileURLToPath(new URL("../dist/", import.meta.url));
const destination = fileURLToPath(new URL("../docs/", import.meta.url));
await checkPages(source);
await rm(destination, { recursive: true, force: true });
await cp(source, destination, { recursive: true });
console.log(
  "Published to docs/. In GitHub Pages settings, select this branch and /docs (not /).",
);
