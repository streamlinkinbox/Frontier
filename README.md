# Frontier

## Environment Editor (WebGPU)

[`environment-editor/`](environment-editor/) — the unified environment
editor prototype: a terrain studio around a true SDF volume with selectable
48–192-cell voxel resolution, sedimentary geology-driven cliffs, landform
presets, SDF erosion, drawn rivers/roads, outcrop scatter, baked SDF AO and
project/map exports (Phase 1, this branch), designed to plug in the 2.5D
shallow-water simulation (`engine/fluid`, real caustics) and
terrain-simulated vegetation (Slate tree/grass generator) behind the same
brush and Frontier Editor UI.

```sh
cd environment-editor
python3 -m http.server 5173 --bind 0.0.0.0   # then open http://localhost:5173/
npm run check                                  # core logic checks (Node)
```

See [`environment-editor/README.md`](environment-editor/README.md) for the
representation, the anti-banding geology/erosion design, controls, and the
Phase 2/3 coupling contracts (fluid bed, flow/moisture, vegetation
suitability).
