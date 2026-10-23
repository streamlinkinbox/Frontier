# Procedural Vegetation — Import Evidence

- Tested revision: `Experimental/ProceduralVegetation/` = `unassignedinbox/Slate@3b1172f78b07`
  (41/41 files blob-identical; see `IMPORT.md` there).
- Platform: Linux sandbox, Node `v22.22.3`, npm `10.9.8`. Not a Windows desktop session.

| Command | Result | Log |
| --- | --- | --- |
| `npm ci --ignore-scripts --no-audit --no-fund` | ✔️ 63 packages, 3 s | — |
| `npm test` (`vitest run`, topology + bark suites) | ✔️ **170/170**, 103 s | `Results/Test-2026-10-23.log` |
| `npm run build` (`tsc --noEmit && vite build`) | ✔️ clean; both entries | `Results/Build-2026-10-23.log` |
| `npm run dev` + `curl` both entries | ✔️ 200 / 200 | live preview (port 5173) |
| `node tools/screenshot.mjs` capture | 🏴 Blocked: `puppeteer-core` undeclared + no Chromium in sandbox | — |

- Validation code is the demo's own retained suite (`Experimental/ProceduralVegetation/tests/`),
  reused unchanged — no copy lives here.
- The browser demo proves the imported demo only; it proves nothing about the native C++ editor.
