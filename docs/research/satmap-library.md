# The 100-map SatMap library · Frontier 0.10

Open **Materials → SatMaps** (`?renderer=webgl&panel=materials`). There are **100 built-in color lookup tables**, plus the existing custom image/strip import slot.

## What's included

| Environment | Maps |
| --- | ---: |
| Desert | 10 |
| Grassland | 8 |
| Forest | 10 |
| Alpine | 8 |
| Fantasy | 12 |
| Lake | 8 |
| Volcanic | 10 |
| Icelandic | 8 |
| Beach | 8 |
| Quarry | 8 |
| Wetland | 6 |
| Badlands | 4 |
| **Total** | **100** |

**Four maps are satellite-derived:** Namib dunes, Canyonlands, Volcanic coast, and White Sands. Their IDs, pixels, original image crops and provenance have not changed.

**The other 96 are original artist-authored CLUTs**, including twelve fictional fantasy palettes. They are not satellite-image extractions, proprietary Gaea assets, measured reflectance, or georeferenced land-cover data. Geographic/theme names describe the color inspiration, not a claim that imagery of that place was sampled. The UI labels each origin explicitly.

The authored maps use compact, distinct seven-color recipes, with linear-light interpolation into the same 256-sample sRGB lookup texture as the original satellite maps. They cover, for example, Highveld summer/winter, rainforests and boreal woods, glaciers, black-sand plains, coral beaches, travertine pools, ironstone pits, copper terraces, and amethyst/opal fantasy environments.

## Browsing

- **Search** names, environment names and descriptive tags such as moss, copper, blue, sand or glacier. Multiple search words are combined.
- **Environment** filters all twelve families, with counts.
- **Provenance** separates satellite-derived maps from authored/fantasy maps.
- The browser displays **12 cards per page**, with Previous/Next and direct page selection.
- **Show selected** clears filters and reveals the current map if it is outside the visible results. Loading a project reveals its selected card when the current filters allow it.
- Authored cards display the actual color ramp, not a fabricated satellite thumbnail. Only the original four image crops are requested.

## Rendering and detail

All 100 maps use the existing **terrain-data mask → CLUT → albedo** path in WebGL2, WebGPU and CPU mesh export. Selecting one does not regenerate the SDF, run erosion, alter the mask weights, or allocate 100 GPU textures. Only the selected palette/detail data is uploaded.

The new recipes **share the existing four attributed 64² satellite-luminance detail tiles** by family. Their source is shown separately from the color provenance. This avoids duplicating detail data or downloading new photographs. It does not imply that a forest palette's detail is a scanned forest material.

These are terrain color maps: Forest does not plant tree geometry, Lake does not create water or change its separate shader, and a Fantasy/Lava palette is not automatically emissive. Lighting, clipping/bias, source detail, and mask controls continue to govern the result.

## Projects and export

Registered IDs are validated against the complete catalog. Existing four-map projects and custom imports remain compatible. New projects save the selected built-in ID and controls, not the entire catalog; reopening requires a build that includes that ID.

The palette download button exports the selected **256 × 1 PNG** before the user's clip/bias/saturation adjustments. GLB terrain export uses the selected CLUT for linear vertex colors, with the same existing vertex-resolution limitations. Custom imported pixels remain embedded in `.frontier` projects as before.

## Maintenance and verification

- `src/engine/satmaps/recipes.ts`: 96 compact authored recipes and the twelve families.
- `src/engine/satmaps/catalog.ts`: deterministic ramp expansion, provenance/detail references, ID registry and search.
- `src/engine/satmaps/library.json`: unchanged original satellite extraction data.
- `src/components/SatMapLibrary.tsx`: paginated browser and filters.

Unit tests check all 100 unique IDs, names and palette byte strings, exact category counts, unchanged original source pixels, linear interpolation, strict validation, shared detail, filtering and project round trips. Browser tests render **every palette through both backends**, comparing its color with the CPU lookup equation, and check UI navigation, provenance, downloads, project restoration and mobile layout.
