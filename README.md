# Frontier — Procedural East-Asian Building Generator

Blender-Geometry-Nodes-style **procedural generator for ancient East-Asian buildings with modern styling**.
Phase 1 implements the roof system: 4 researched roof types, 3 tile systems, full timber
structure, ornaments — plus a live verification panel that proves nothing floats.

```bash
npm install
npm run dev      # → http://localhost:5173
npm run build
```

Export any roof as **.GLB** (imports straight into Blender for Geometry Nodes work) or as **.JSON params**.

## Phase 1 — Roofs 屋根

**Styles** — the four canonical types shared by Japan, China and Korea:

| Style | JP | CN / KR | Form |
|---|---|---|---|
| Kirizuma | 切妻造 | 悬山/硬山 · matbae | gabled, 2 slopes + gable walls |
| Yosemune | 寄棟造 | 庑殿 · udjin-gak | hipped, 4 slopes, 45° plan hips (equal pitch) |
| Irimoya | 入母屋造 | 歇山 · paljak | hip-and-gable: upper gable roof astride a lower hip roof |
| Hōgyō | 宝形造 | 攒尖 | pyramidal, 4 hips meet at an apex + finial |

**Tiles**: hongawara-buki 本瓦葺 (concave hiragawara + half-round marugawara + round
nokigawara eave caps), sangawara-buki 桟瓦葺 S-pantiles, and modern flat interlocking
panels for 和モダン styling. Real kawara module (270 mm / 225 mm exposure).

**Curvature**: concave sorimashi profile (nawadarumi slack-rope approximation), curled
hip rafters (sori), and eave corner upturn (反り / jiaoqiao / cheoma).

**Structure**: round taruki rafters at 455 mm pitch, wall plates, mid purlins, ridge
beam (munagi), tie beam + king post, bargeboards (straight or karahafu ogee),
kirikomi flashing, onigawara demon tiles, hōju apex finial.

**Verification** (live panel): eave overhang, pitch range, curvature, eave height, tile
coverage, hip closure — and a **connectivity check** proving every part touches the
ground→plinth→walls→roof chain (no floating geometry).

## Phase 1b — Ornaments & lanterns 提灯

**Chinese ornament pack** (match the glazed-palace look): chiwen 螭吻 ridge-end beasts
that "swallow" the ridge, wenshou hip beasts in rank-coded odd numbers (3–9 per hip),
dougong-style painted bracket sets carrying the eaves, imperial orange/yellow glaze
swatches. Japanese onigawara retained as the JP option.

**Lantern generator** — 3 independent groups, each with 6 procedural designs:

| Design | Form |
|---|---|
| Tube chōchin 筒提灯 | ribbed paper cylinder, hung (aka = izakaya red) |
| Round chōchin 丸提灯 | squashed paper sphere |
| Andon 行灯 | wooden-frame box lamp, Edo style |
| Tall kiriko 切子 | 1 m festival box lantern on feet |
| Paper globe 明かり | globe on tripod stand |
| Stone tōrō 石灯籠 | 6-part: base, pillar, platform, fire box, roof, jewel |

Each group: eave-hung (cords tie under the slopes) or standing placement, count,
size, glow, paper/frame colors, and **custom characters drawn vertically on the
paper**. Real point lights optional; **Night mode** in the viewer shows them off.

`npm run verify` headlessly builds 47 scenarios (styles × tiles × ornaments ×
lanterns × extremes) and asserts zero floating parts.

## Research sources

- Roof types kirizuma / yosemune / irimoya / hōgyō — adayofzen.com, meguri-japan.com, note.com/kominkanist
- Hongawara construction (hiragawara + marugawara), sangawara history, ~104 kg/m² — gov-online.go.jp (Nihongawara), isaackremer.com
- Onigawara dimensions (~28 cm) — Japan Antique Roadshow (tokaido.wordpress.com)
- Sori / curled hip rafters — thecarpentryway.blog; nawadarumi curve — engineerfix.com
- Juzhe curvature, flying rafters, corner upturn (jiaoqiao) — MDPI Buildings 2025
- CN roof hierarchy wudian › xieshan › xuanshan › yingshan; parametric rules — Shen et al., ResearchGate

## Roadmap

- **Phase 2** — Walls, floors, building types (machiya / store / office …)
- **Phase 3** — Openings, lattices, noren & signboards (custom text)
- **Phase 4** — Lights (types), utility poles & wiring, electricity hookup
- **Phase 5** — Furniture & street props (all Asian-style, all optional)
