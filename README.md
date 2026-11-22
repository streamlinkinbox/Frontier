# FRONTIER — Multi-Biome Level Observer & Off-Road World Sandbox

An AA-style 3D multi-biome off-road world prototype built with **Three.js + TypeScript + Vite**, featuring an **Unreal Engine-style WASD Free-Fly Observer Camera** (`src/camera/unrealCamera.ts`), **passive world inspection** (animals, rocks, mudslides, and vines animate naturally in the world without attacking your vehicle), and instant switching across **3 distinct levels**.

## Levels (`[1]`, `[2]`, `[3]`, `[P]` or Portal Fly-Through)

1. **`[1]` Forest Level (`PRIMEVAL TITAN FOREST`)**:
   - Spacious `68m–104m` natural clearings (`320m` wide valley), colossal Titan trees, cascading canopy vines, snare roots, split routes, giant hollow log tunnels, creek puddles, falling trees, and **Flash-Flood Mudslides & Rolling Boulders**.
2. **`[2]` Swamp Level (`BAYOU MEGAROOT SWAMP`)**:
   - Misty bioluminescent wetlands interlaced with **Static Crisscrossing Giant Ground Roots** (`4.7m`-wide wooden root bridges weaving over and under lagoons), plus passively patrolling **Siphon Mosquitos** and **Robotic Ticks**.
3. **`[3]` Desert Level (`SCORCHED SCORPION CANYON`)**:
   - Sweeping golden sand dunes, towering red-rock sandstone arches & mesas, swirling **Dust Devil** vortexes, canyon boulders, and **Robotic Ticks**.

## Unreal WASD Observer Controls
- **Hold Right-Click or Left-Click + Drag Mouse** (or **Arrow Keys**) — 360° Pitch & Yaw Look
- **`W / A / S / D`** — Fly through the course in 3D look direction
- **`E` / `Space`** — Fly Up (`+Y`)
- **`Q` / `C`** — Fly Down (`-Y`)
- **`Shift`** — `3.2x` Fast Sprint Flight
- **`Mouse Scroll Wheel`** — Adjust base camera flight speed (`6 m/s` – `260 m/s`)
- **`1` / `2` / `3` / `P`** — Switch immediately between **Forest**, **Swamp**, and **Desert** levels (or fly through the Portal Gate at `-860m`)
- **`J`** (or HUD Landmark Buttons) — Jump `+200m` down the course or directly to key zones
- **`T`** — Place parked showcase vehicle (`NOMAD`, `TITAN`, `MANTIS`, `IRONCLAD`) ahead of camera
- **`V`** — Toggle between **Unreal Free-Cam** and **Peaceful Drive** (drive through the course with zero hazard attacks)
