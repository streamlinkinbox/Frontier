# Frontier

## Playable levels

### 🌿 Level C — Overgrown Bio-Lab / Carnivorous Jungle Canopy (`biolab/`)

A playable 3D greybox ("shadow-box") level. Race the Pack Runner 1.44 km from the Airlock to
the Extraction Pad while the lab itself tries to stop you — Snapper Jaws bite your lane, Vine
Grabbers snare you dead (mash to cut free, or burn through on boost), Spore Puffers choke the
engine, Sap Pools glue you down, Bramble Walls and cycling Blast Doors block the road, and the
Mother Maw sweeps the Heart Chamber with living tentacles.

**Simplified pack-driving controls** — auto-drive is always on; you just:
`◀ ▶`/`A D` steer · `W` boost · `S` brake · `SPACE` cutter pulse · touch supported.

**Run it** (no build step, Three.js is vendored):

```bash
python3 -m http.server 8000
# open http://localhost:8000/  →  Play Level C
```

Full design breakdown — zone pacing, hazard specs, tuning notes and playtest questions — in
[`biolab/LEVEL_DESIGN.md`](biolab/LEVEL_DESIGN.md).
