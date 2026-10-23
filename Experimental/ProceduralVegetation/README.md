# Frontier — procedural vegetation generator

This checkout contains the TypeScript/Three.js vegetation workspace. It generates production-oriented,
wind-ready plant meshes with one closed welded surface per plant and exports OBJ/GLB for downstream DCC/game
workflows.

```bash
npm install
npm run dev
npm test
npm run build
```

## Desert plant library

The Desert group now includes the existing Joshua Tree and Desert Ironwood plus seven new xeric presets:

- Saguaro Cactus
- Golden Barrel Cactus
- Organ Pipe Cactus
- Agave Americana
- Aloe Vera
- Ocotillo
- Dasylirion Sotol

The presets stay on the same deterministic welded-quad pipeline, so they inherit the topology validation,
wind attributes, OBJ/GLB export and inspector tooling. Cactus and succulent selections also switch the viewport
to a warmer desert presentation, waxier stem/leaf materials, and a procedural rib/areole shader treatment.

The new presets are intentionally conservative about junction density: rosette leaves and low-radius stems
keep their attachment windows open at the default radial resolution, and the topology suite covers all seeds
used by the existing species matrix.
