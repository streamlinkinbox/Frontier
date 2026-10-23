# Import — Procedural Vegetation Demo

## Identity

| Field | Value |
| --- | --- |
| Source repository | `unassignedinbox/Slate` |
| Source branch | `arena/01a096c9-slate` |
| Source revision | `3b1172f78b07` — "Add isolated procedural bark cluster laboratory" (2026-10-08) |
| Vegetation app commits | `05d9be23` desert library + app import · `3608afd4` shader fix · `3b1172f7` bark entry |
| Imported | 2026-10-23 into `Experimental/ProceduralVegetation/` on `arena/10a5e6ab-frontier` |
| Files | 41 pinned files, 676 KB — verified **41/41 byte-identical** to the source blobs |
| Not imported | The Slate engine tree on that branch (`Engine/`, `Projects/`, patches) — demo only, per owner |
| Entry points | `index.html` vegetation generator (51 presets: 37 trees/cacti + 14 grasses) · `bark-lab.html` |

## Rules applied

- Imported source retains upstream names and contents (including non-compliant legacy names such as
  `mesh.ts` / `mesher.ts`); they are reference material, not new Frontier naming. Renames happen only
  when code graduates out of `Experimental/`, by the `SKILL-Naming.md` construction procedure.
- Only additions beside the pinned files: this `IMPORT.md` and `.gitignore` (`node_modules/`, `dist/`,
  `.vite/`, `coverage/`) mirroring the source branch's web-generator ignores.
- Dependencies install from the pinned `package-lock.json`; nothing third-party is vendored here.

## Run + verify (executed 2026-10-23, Linux sandbox, Node 22, npm 10)

```sh
cd Experimental/ProceduralVegetation
npm ci --ignore-scripts --no-audit --no-fund
npm test          # 170/170 passed
npm run build   # tsc clean + both HTML entries built
npm run dev     # http://localhost:5173  (bound 0.0.0.0, hosted preview compatible)
```

| Check | Result |
| --- | --- |
| Blob identity vs `3b1172f7` | ✔️ 41/41 identical |
| `vitest run` (topology + bark, 103 s) | ✔️ 170/170 — `Exhibits/ProceduralVegetation/Results/` |
| `tsc --noEmit && vite build` | ✔️ clean (one >500 KB chunk warning: `three` OrbitControls, upstream) |
| Dev server | ✔️ `GET /` 200, `GET /bark-lab.html` 200, live preview started |
| Screenshot capture (`tools/screenshot.mjs`) | 🏴 Blocked: `puppeteer-core` undeclared, no Chromium |

## Role in the one-editor foliage merge

This is import **#1** for the unified foliage/plant editor (owner direction 2026-10-23).
Plan: `Documentation/Plans/Ongoing/ProceduralVegetation.md`. Nothing is integrated into `Engine/`
or the native editor yet; the demo must not be presented as native editor rendering.
