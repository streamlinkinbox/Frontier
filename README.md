# Frontier

A procedural generator for **East-Asian ancient-style buildings with modern
styling** — every building generated from parameters, in the spirit of Blender
geometry nodes, not a fixed asset.

Work is deliberately staged: **the roof comes first**, because a Chinese roof is
the hardest and most characteristic part, and everything else has to sit under
it.

## Where it is now

The roof generator is complete and verified for all four classical forms:

| 屋頂 form | `type` | what it is |
|---|---|---|
| 廡殿 | `hip` | four slopes, one ridge, hips at every corner |
| 歇山 / 入母屋 | `hipGable` | hip skirt below, gable above, 博脊 break line |
| 懸山 | `gable` | two slopes, roof oversails the gable wall |
| 攢尖 | `pyramid` | four hips massing to an apex with a 寶頂 |

```bash
npm run typecheck    # tsc --noEmit
npm test             # generate + verify all four types; exit code = pass/fail
npm run shot         # headless software render → roof.bmp → convert to PNG
```

`npm test` prints a full report; every line is a real geometric fact, not a
smoke test.

## How it works

```
src/core/juzhe.ts   舉折 — the Song-dynasty profile rule. Purlin heights drop
                    H0/10, then halve each step. This file is the single source
                    of truth for the roof's SHAPE.

src/gen/roofField.ts  The height field s(x,z) = min over faces of (perpendicular
                    distance from that face's eave ÷ its depth). Taking the MIN
                    means a hip is the exact intersection of two faces and the
                    ridge is exactly level — nothing floats, nothing crosses.
                    Everything else reads its position from this field.

src/gen/tileKit.ts  Tile and ornament solids: 板瓦, 筒瓦, 瓦當, 滴水, 脊瓦, 鴟吻.

src/gen/tiles.ts     Tile course layout. 壓六露四 overlap, columns on a pitch,
                    hip tiles cut to the hip line.

src/gen/frame.ts     The structural core: 椽 rafters purlin-to-purlin, 飛椽
                    flying rafters carrying the concave eave, 檩 purlins on the
                    舉折 line, 角梁 hip rafters, 望板 sheathing whose top face IS
                    the tile bedding plane, 柱 columns, 斗拱 brackets.

src/gen/ridges.ts    脊瓦 on every ridge run, with 正吻 / 鬼瓦 / 寶頂.

src/gen/verify.ts   20 geometric checks. This is the acceptance test.

src/gen/index.ts    generateRoof(request) → { field, tiles, frame, ridges, geometries }
```

### The no-float contract

Everything is placed by reading back **from** the surface, never by adding up
heights, and `verify.ts` re-derives each of these independently:

1. rafters run purlin to purlin and their tops meet the sheathing's underside;
2. purlin tops lie exactly on the 舉折 profile;
3. sheathing's top face is exactly the tile bedding plane;
4. 板瓦 bed on a 灰漿 layer above that plane and 筒瓦 crown on the pans;
5. 脊瓦 rims reach down to the 筒瓦 crowns either side, so a ridge straddles its
   tiles instead of hovering;
6. columns top out exactly under the eave purlin.

Two datums, deliberately kept apart: `yAt()` is the **tile bedding plane**
(the 舉折 line offset along its own normal by the rafter + sheathing stack),
`timberY()` is the **structural 舉折 line** that purlins and columns sit on.

### One kernel, two label sets

`roofType` picks geometry. `style` (`chinese` | `japanese`) changes only naming,
tile defaults and ornament set — never the geometry kernel.

## Verification is the point

`src/gen/verify.ts` re-derives geometry from scratch and reports the worst
residual in metres per check. Current state: **all checks pass on all four roof
types**, most at machine precision (e.g. tile ends on the bedding plane:
4e-16 m). The checks that carry real tolerance are the honest ones — a rigid
tile bridging a concave eave, or a 脊瓦 chording the 翼角 sweep.

## Parametric surface (what the UI will drive)

`RoofRequest` in `src/gen/index.ts` covers footprint, 舉高 rise ratio, 折屋 step
count, 出檐 overhang, 飛椽 geometry, corner upturn (起翹/翼角), tile preset and
scale, 筒瓦 vs 板瓦-only roofs, frame member sizes, and ornament set.
`ROOF_PRESETS` holds named starting points (palace, hall, house, pavilion, minka).

## Research

`docs/roof-research.md` is the authoritative reference: Chinese and Japanese roof
taxonomy, the 舉折 algorithm with its closed form and worked example, eave and
flying-rafter construction, corner pad blocks, the height-field method, the
入母屋 break line, and the tile system with overlaps and glaze rank. Ten sources.
