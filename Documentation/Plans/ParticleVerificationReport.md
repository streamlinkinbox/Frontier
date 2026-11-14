# Frontier Particle System — Exhaustive 93/93 Per-Preset CPU + Vulkan 1.3 GPU Verification Report

> **Status:** `93 / 93` Active Presets Individually Verified PASS · `9 / 9` Quarantined Slugs Rejected · `0` NaN/Inf · **100% Live Vulkan 1.3 GPU Compute + Rasterization**
> **Vulkan 1.3 Device:** `SwiftShader Device (Subzero)` (`23/23` SPIR-V 1.3 shaders compiled with zero `DrawParameters`)

## Hero Visual Proofs

- **All 93/93 Individual Presets Contact Sheet 1 (Presets #01–#31):** `Exhibits/Images/Particles/ParticleProof_All93_Sheet1_Presets01_31.png`
- **All 93/93 Individual Presets Contact Sheet 2 (Presets #32–#62):** `Exhibits/Images/Particles/ParticleProof_All93_Sheet2_Presets32_62.png`
- **All 93/93 Individual Presets Contact Sheet 3 (Presets #63–#93):** `Exhibits/Images/Particles/ParticleProof_All93_Sheet3_Presets63_93.png`
- **3D Oriented Solids Showcase (Ferrofluid Spikes, Winged Boids, Fracture Shards, MoGraph Cubes):** `Exhibits/Images/Particles/ParticleProof_Gpu3DOrientedSolids_Quad.png`
- **GPU 3D `SignedDistanceVolume` SDF Collision Bounce:** `Exhibits/Images/Particles/ParticleProof_GpuSdfCollider_Bounce.png`
- **Depth-Composited HDR World Integration (`TieredWorldRenderSequence` RTX Tier 2):** `Exhibits/Images/Particles/ParticleProof_WorldIntegration_DepthComposite.png`

## Complete 93 / 93 Individual Preset Telemetry & Parameters Matrix

| # | Slug | Display Name | Domain | GPU Render Mode | Cap | Rate/s | Speed (m/s) | Life (s) | CPU Alive | GPU Alive | GPU Peak (m/s) | GPU Bounds (m) | GPU Exec | Lit Px | NaN | Result |
|---:|---|---|---|---:|---:|---:|---|---|---:|---:|---:|---:|:---:|---:|---:|:---:|
| 1 | `coin-fountain` | Coin fountain | Universal | 3D Solid Coin | 400 | 0.00 | 5.00–8.00 | 6.00–9.00 | 400 | 400 | 3.53 | 5.36 | YES | 1493 | 0 | **PASS** |
| 2 | `magnetic-particles` | Magnetic particles | Universal | GPU Spark Streak | 1200 | 220.00 | 0.00–0.40 | 6.00–10.00 | 1200 | 1200 | 6.12 | 12.12 | YES | 1358 | 0 | **PASS** |
| 3 | `bubbles` | Bubbles | Universal | GPU Soft Disc | 400 | 25.00 | 0.20–0.60 | 5.00–8.00 | 216 | 216 | 5.15 | 10.03 | YES | 670 | 0 | **PASS** |
| 4 | `foam` | Foam | Universal | GPU Soft Disc | 1500 | 500.00 | 0.20–1.00 | 4.00–7.00 | 1497 | 1497 | 3.50 | 3.48 | YES | 1548 | 0 | **PASS** |
| 5 | `sparks` | Sparks | Universal | GPU Spark Streak | 3000 | 160.00 | 2.00–6.00 | 0.60–1.60 | 170 | 170 | 5.25 | 2.78 | YES | 314 | 0 | **PASS** |
| 6 | `rain` | Rain streaks | Universal | GPU Spark Streak | 5500 | 850.00 | 8.50–11.50 | 1.00–1.60 | 1096 | 1096 | 11.11 | 6.14 | YES | 2309 | 0 | **PASS** |
| 7 | `hail` | Hail | Universal | 3D Solid Shard | 2000 | 160.00 | 7.20–10.50 | 2.50–4.00 | 515 | 515 | 9.44 | 7.30 | YES | 4874 | 0 | **PASS** |
| 8 | `leaves` | Leaves | Universal | 3D Solid Coin | 1200 | 20.00 | 0.20–0.90 | 7.00–12.00 | 206 | 206 | 2.94 | 8.53 | YES | 833 | 0 | **PASS** |
| 9 | `paper` | Paper & debris | Universal | 3D Solid Coin | 600 | 8.00 | 0.10–0.50 | 6.00–10.00 | 140 | 140 | 3.79 | 7.07 | YES | 915 | 0 | **PASS** |
| 10 | `fire` | Fire | Universal | GPU Spark Streak | 2500 | 280.00 | 0.60–1.70 | 0.70–1.50 | 311 | 311 | 4.14 | 2.27 | YES | 496 | 0 | **PASS** |
| 11 | `smoke` | Smoke | Universal | GPU Spark Streak | 1500 | 70.00 | 0.30–0.80 | 3.00–6.00 | 308 | 308 | 4.33 | 10.06 | YES | 1003 | 0 | **PASS** |
| 12 | `explosion` | Explosion | Universal | GPU Spark Streak | 2000 | 0.00 | 3.00–9.00 | 0.50–1.20 | 75 | 75 | 6.41 | 1.87 | YES | 742 | 0 | **PASS** |
| 13 | `tornado` | Tornado debris | Universal | 3D Solid Coin | 1500 | 90.00 | 1.00–3.00 | 4.00–7.00 | 504 | 504 | 5.57 | 7.80 | YES | 1004 | 0 | **PASS** |
| 14 | `sandstorm` | Sandstorm | Universal | GPU Spark Streak | 6000 | 900.00 | 5.00–8.00 | 4.00–7.00 | 4669 | 4669 | 7.43 | 9.79 | YES | 1525 | 0 | **PASS** |
| 15 | `snow` | Snow | Universal | GPU Soft Disc | 3500 | 280.00 | 0.45–1.00 | 8.00–12.00 | 1636 | 1636 | 4.47 | 9.84 | YES | 3341 | 0 | **PASS** |
| 16 | `embers` | Embers | Universal | GPU Spark Streak | 900 | 45.00 | 0.80–2.20 | 2.00–4.20 | 132 | 132 | 4.80 | 6.81 | YES | 245 | 0 | **PASS** |
| 17 | `petals` | Cherry petals | Universal | 3D Solid Coin | 700 | 14.00 | 0.20–0.70 | 7.00–11.00 | 173 | 173 | 2.70 | 5.65 | YES | 1115 | 0 | **PASS** |
| 18 | `sand` | Sand grains | Universal | GPU Soft Disc | 4000 | 350.00 | 0.80–2.20 | 2.50–4.00 | 1129 | 1129 | 6.11 | 4.76 | YES | 1141 | 0 | **PASS** |
| 19 | `ash` | Ash flakes | Universal | GPU Spark Streak | 1500 | 30.00 | 0.30–1.00 | 6.00–10.00 | 261 | 261 | 3.38 | 7.53 | YES | 502 | 0 | **PASS** |
| 20 | `debris` | Small debris | Universal | GPU Spark Streak | 800 | 30.00 | 2.00–5.00 | 3.00–5.00 | 122 | 122 | 4.88 | 3.63 | YES | 253 | 0 | **PASS** |
| 21 | `flares` | Countermeasure flares | Universal | GPU Spark Streak | 480 | 0.00 | 8.50–13.50 | 3.40–5.20 | 362 | 362 | 12.17 | 5.65 | YES | 3506 | 0 | **PASS** |
| 22 | `pollen` | Pollen & dust motes | Universal | GPU Spark Streak | 2000 | 90.00 | 0.05–0.20 | 8.00–12.00 | 591 | 591 | 4.59 | 8.65 | YES | 640 | 0 | **PASS** |
| 23 | `watermist` | Water mist | Universal | GPU Soft Disc | 1800 | 220.00 | 0.90–2.60 | 1.90–3.80 | 624 | 624 | 3.66 | 4.88 | YES | 3913 | 0 | **PASS** |
| 24 | `glass` | Glass shards | Universal | 3D Solid Shard | 420 | 26.00 | 2.20–5.40 | 2.20–3.80 | 75 | 75 | 5.09 | 5.99 | YES | 741 | 0 | **PASS** |
| 25 | `lapilli` | Volcanic ash & lapilli | Universal | GPU Soft Disc | 500 | 25.00 | 6.00–12.00 | 3.00–5.00 | 99 | 99 | 8.22 | 7.06 | YES | 601 | 0 | **PASS** |
| 26 | `seeds` | Dandelion seeds | Universal | GPU Spark Streak | 400 | 15.00 | 0.10–0.40 | 8.00–12.00 | 178 | 178 | 3.45 | 6.59 | YES | 491 | 0 | **PASS** |
| 27 | `feathers` | Feathers | Universal | 3D Solid Coin | 300 | 6.00 | 0.10–0.30 | 8.00–12.00 | 129 | 129 | 2.67 | 5.41 | YES | 940 | 0 | **PASS** |
| 28 | `confetti` | Confetti & streamers | Universal | 3D Solid Coin | 1200 | 25.00 | 1.00–3.00 | 5.00–8.00 | 221 | 221 | 4.24 | 6.70 | YES | 1034 | 0 | **PASS** |
| 29 | `steam` | Steam vent | Universal | GPU Spark Streak | 1600 | 60.00 | 0.80–1.60 | 3.00–5.00 | 239 | 239 | 3.55 | 5.25 | YES | 610 | 0 | **PASS** |
| 30 | `fireworks` | Fireworks | Pyro Volley | GPU Spark Streak | 1200 | 0.00 | 4.00–9.00 | 1.80–2.90 | 141 | 129 | 3.87 | 2.87 | YES | 1444 | 0 | **PASS** |
| 31 | `fireworks-spectacle` | New Year's Text & Shape Fireworks | Pyro Volley | GPU Spark Streak | 1400 | 0.00 | 4.00–9.00 | 2.00–3.20 | 280 | 256 | 4.44 | 3.62 | YES | 1551 | 0 | **PASS** |
| 32 | `curl-fur-field` | Curl Wave Field | Universal | GPU Spark Streak | 10000 | 1800.00 | 0.30–1.10 | 2.80–4.60 | 6649 | 6649 | 3.18 | 5.73 | YES | 1995 | 0 | **PASS** |
| 33 | `curl-sphere-plasma` | Spherical Curl Plasma | Universal | GPU Spark Streak | 9000 | 1500.00 | 0.40–1.60 | 3.00–5.20 | 6137 | 6137 | 3.46 | 6.40 | YES | 2184 | 0 | **PASS** |
| 34 | `bokeh-stream-ribbons` | Bokeh Stream Ribbons | Universal | GPU Glow Orb | 3200 | 580.00 | 0.30–1.00 | 4.50–7.50 | 3164 | 3164 | 3.64 | 7.16 | YES | 10897 | 0 | **PASS** |
| 35 | `bokeh-object-pulse` | Surface Pour Ribbons | Universal | GPU Glow Orb | 3400 | 620.00 | 0.40–1.10 | 3.40–5.20 | 2691 | 2691 | 3.69 | 7.07 | YES | 3384 | 0 | **PASS** |
| 36 | `tesla-sphere-lightning` | Tesla Sphere Lightning | Universal | GPU Spark Streak | 600 | 0.00 | 1.80–5.20 | 0.16–0.42 | 27 | 27 | 3.92 | 0.80 | YES | 426 | 0 | **PASS** |
| 37 | `static-sphere-shock` | Static Shock Arcs | Universal | GPU Glow Orb | 600 | 0.00 | 1.20–3.40 | 0.14–0.36 | 24 | 24 | 2.43 | 0.49 | YES | 308 | 0 | **PASS** |
| 38 | `shockwave-ring` | Blast & Sonic Shockwave | Universal | GPU Soft Disc | 96 | 2.20 | 0.02–0.14 | 0.72–1.05 | 2 | 2 | 0.65 | 0.18 | YES | 3016 | 0 | **PASS** |
| 39 | `emp-pulse-dome` | EMP Energy Pulse Dome | Universal | GPU Glow Orb | 96 | 1.10 | 0.00–0.05 | 0.90–1.25 | 2 | 2 | 2.45 | 0.46 | YES | 1532 | 0 | **PASS** |
| 40 | `radar-sweep` | Tactical Radar Sweep & Blips | Universal | GPU Soft Disc | 16 | 0.25 | 0.00–0.00 | 7.50–8.50 | 11 | 11 | 3.19 | 9.99 | YES | 3826 | 0 | **PASS** |
| 41 | `sonar-ping` | Acoustic Sonar Pulse Ping | Universal | GPU Soft Disc | 64 | 1.20 | 0.00–0.04 | 1.10–1.50 | 2 | 2 | 2.84 | 1.15 | YES | 4261 | 0 | **PASS** |
| 42 | `plasma-laser-beam` | Plasma & Laser Beam | Universal | GPU Spark Streak | 1200 | 260.00 | 3.20–9.50 | 0.18–0.55 | 105 | 105 | 7.78 | 1.39 | YES | 409 | 0 | **PASS** |
| 43 | `plasma-tracers` | Bullet & Plasma Tracers | Universal | GPU Spark Streak | 1200 | 36.00 | 19.00–25.00 | 0.55–0.95 | 27 | 27 | 23.87 | 6.00 | YES | 781 | 0 | **PASS** |
| 44 | `projectile-trails` | Glowing Projectile Trails | Universal | GPU Glow Orb | 256 | 14.00 | 6.40–9.20 | 2.00–3.10 | 36 | 36 | 7.74 | 5.94 | YES | 540 | 0 | **PASS** |
| 45 | `welding-grinder-sparks` | Welding & Grinder Arc Spray | Universal | GPU Spark Streak | 3200 | 420.00 | 7.00–13.80 | 0.65–1.45 | 439 | 439 | 12.93 | 4.86 | YES | 1528 | 0 | **PASS** |
| 46 | `hologram-data-glitch` | Hologram & Signal Glitch | Universal | GPU Spark Streak | 900 | 110.00 | 0.85–1.95 | 1.80–3.30 | 279 | 279 | 5.85 | 9.10 | YES | 1315 | 0 | **PASS** |
| 47 | `boids-flock` | Boids Flock (Birds / Bats / Fish) | Boids Flock | 3D Winged Boid | 220 | 48.00 | 2.20–3.80 | 12.00–18.00 | 220 | 220 | 3.38 | 4.37 | YES | 1030 | 0 | **PASS** |
| 48 | `boids-butterflies` | Butterfly & Wisp Swarm | Boids Flock | 3D Winged Boid | 180 | 36.00 | 1.20–2.30 | 10.00–16.00 | 180 | 180 | 3.45 | 4.72 | YES | 1629 | 0 | **PASS** |
| 49 | `boids-fish-school` | Bioluminescent Fish School | Boids Flock | 3D Winged Boid | 260 | 64.00 | 2.10–3.40 | 14.00–22.00 | 260 | 260 | 4.19 | 4.31 | YES | 1284 | 0 | **PASS** |
| 50 | `viscous-dripping` | Viscous Dripping & Trails | Universal | GPU Strand Ribbon | 320 | 26.00 | 0.04–0.24 | 1.80–2.90 | 63 | 63 | 4.26 | 2.50 | YES | 1156 | 0 | **PASS** |
| 51 | `derez-cube` | Derez Cube | Derez Solid | 3D Solid Cube | 1536 | 0.00 | 0.60–2.60 | 3.00–4.00 | 784 | 784 | 4.19 | 13.35 | YES | 1146 | 0 | **PASS** |
| 52 | `cube-coins` | Cube to Coins | Derez Solid | 3D Solid Coin | 384 | 0.00 | 1.50–4.00 | 4.00–6.00 | 361 | 361 | 3.82 | 11.17 | YES | 1713 | 0 | **PASS** |
| 53 | `melt-away` | Melt Away | Derez Solid | 3D Solid Cube | 1536 | 0.00 | 0.20–0.90 | 6.00–8.00 | 784 | 784 | 4.19 | 13.35 | YES | 1149 | 0 | **PASS** |
| 54 | `reverse-derez` | Reverse Derez | Derez Solid | 3D Solid Cube | 1536 | 0.00 | 0.50–2.00 | 3.00–4.00 | 784 | 784 | 4.19 | 13.35 | YES | 2757 | 0 | **PASS** |
| 55 | `voxel-explosion` | Voxel Explosion | Derez Solid | 3D Solid Cube | 1024 | 0.00 | 2.00–4.50 | 2.50–4.00 | 784 | 784 | 4.19 | 13.35 | YES | 2276 | 0 | **PASS** |
| 56 | `coin-stack` | Coin Stack | Derez Solid | 3D Solid Coin | 64 | 0.00 | 0.00–0.00 | 1.00–1.00 | 64 | 64 | 3.43 | 10.06 | YES | 294 | 0 | **PASS** |
| 57 | `dissolve-nothing` | Dissolve to Nothing | Derez Solid | 3D Solid Cube | 1536 | 0.00 | 0.10–0.60 | 2.50–4.00 | 784 | 784 | 4.19 | 13.35 | YES | 1148 | 0 | **PASS** |
| 58 | `dissolve-butterflies` | Dissolve into Butterflies | Derez Solid | 3D Solid Cube | 1536 | 0.00 | 0.20–0.90 | 6.00–9.00 | 784 | 784 | 4.19 | 13.35 | YES | 1173 | 0 | **PASS** |
| 59 | `glitch-dissolve` | Glitch Dissolve | Derez Solid | 3D Solid Cube | 1536 | 0.00 | 0.10–0.60 | 2.50–4.00 | 784 | 784 | 4.19 | 13.35 | YES | 1148 | 0 | **PASS** |
| 60 | `black-hole` | Black Hole Accretion | SpaceDistort | GPU Spark Streak | 3000 | 400.00 | 0.00–0.20 | 6.00–10.00 | 1395 | 1395 | 4.99 | 8.18 | YES | 1328 | 0 | **PASS** |
| 61 | `heat-shimmer` | Heat Shimmer Distortion | SpaceDistort | GPU Spark Streak | 200 | 30.00 | 0.60–1.40 | 2.00–3.50 | 174 | 174 | 4.16 | 9.98 | YES | 2820 | 0 | **PASS** |
| 62 | `strikeSparks` | Arc Strike Sparks | Universal | GPU Spark Streak | 1600 | 0.00 | 1.80–7.50 | 0.45–1.15 | 63 | 63 | 6.28 | 2.23 | YES | 132 | 0 | **PASS** |
| 63 | `energy-shield-hex` | Energy Shield & Hex Ripples | Energy Shield | GPU Glow Orb | 24 | 0.28 | 0.00–0.00 | 6.50–7.50 | 64 | 64 | 0.00 | 1.83 | YES | 1681 | 0 | **PASS** |
| 64 | `energy-shield-prism` | Multi-Shape Prism Barrier | Energy Shield | GPU Glow Orb | 24 | 0.28 | 0.00–0.00 | 6.50–7.50 | 64 | 64 | 0.00 | 1.83 | YES | 4315 | 0 | **PASS** |
| 65 | `aurora-borealis` | Aurora Borealis Curtains | Universal | GPU Spark Streak | 180 | 16.00 | 0.08–0.24 | 6.50–10.50 | 148 | 148 | 4.22 | 6.89 | YES | 27915 | 0 | **PASS** |
| 66 | `spiderweb-orb` | Orb-Weave Spiderweb | Universal | GPU Spark Streak | 320 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 320 | 320 | 3.90 | 5.69 | YES | 2056 | 0 | **PASS** |
| 67 | `spiderweb-cobweb` | Messy Attic Cobwebs | Universal | GPU Spark Streak | 300 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 300 | 300 | 1.51 | 4.00 | YES | 850 | 0 | **PASS** |
| 68 | `cables-chains` | Swinging Cables & Chains | XPBD Strand | GPU Spark Streak | 64 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 38.37 | 2.02 | YES | 1306 | 0 | **PASS** |
| 69 | `vines-ropes` | Hanging Vines & Ropes | XPBD Strand | 3D Solid Coin | 96 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 36.59 | 1.87 | YES | 3586 | 0 | **PASS** |
| 70 | `flag-banner` | Wind Banner & Tearing Cloth | XPBD Strand | GPU Spark Streak | 64 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 200 | 200 | 16.08 | 1.44 | YES | 655 | 0 | **PASS** |
| 71 | `ship-sail` | Full-Rigged Ship Sail | XPBD Strand | GPU Spark Streak | 64 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 200 | 200 | 27.58 | 1.38 | YES | 2104 | 0 | **PASS** |
| 72 | `grass-wheat-field` | Grass & Golden Wheat Field | XPBD Strand | 3D Solid Coin | 520 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 23.97 | 1.77 | YES | 1592 | 0 | **PASS** |
| 73 | `tall-reeds` | Tall Lakeside Reeds | XPBD Strand | GPU Spark Streak | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 19.40 | 1.94 | YES | 1175 | 0 | **PASS** |
| 74 | `interactive-fern` | Lush Interactive Fern | XPBD Strand | 3D Solid Coin | 128 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 31.62 | 2.00 | YES | 952 | 0 | **PASS** |
| 75 | `interactive-tree` | Swaying Interactive Canopy Tree | XPBD Strand | 3D Solid Coin | 520 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 45.82 | 2.62 | YES | 3751 | 0 | **PASS** |
| 76 | `effector-grid-cubes` | Effector Cube Grid · Height Up/Down | MoGraph | 3D Solid Cube | 576 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 576 | 576 | 2.53 | 3.58 | YES | 2439 | 0 | **PASS** |
| 77 | `effector-sphere-dots` | Halftone Effector Sphere · Circles | MoGraph | GPU Soft Disc | 720 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 676 | 676 | 2.28 | 3.89 | YES | 1528 | 0 | **PASS** |
| 78 | `effector-hand-halftone` | Obsidian Hand · Halftone & Tiles | MoGraph | GPU Soft Disc | 1200 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 784 | 784 | 2.79 | 4.20 | YES | 1472 | 0 | **PASS** |
| 79 | `effector-torus-hex` | Kinetic Hex Torus Grid | MoGraph | GPU Soft Disc | 760 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 729 | 729 | 2.55 | 4.05 | YES | 1549 | 0 | **PASS** |
| 80 | `effector-object-placer` | Placeable 3D Specimen · MoGraph & Effectors | MoGraph | GPU Soft Disc | 1400 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 784 | 784 | 2.79 | 4.20 | YES | 2304 | 0 | **PASS** |
| 81 | `ferrofluid-spikes` | Magnetic Ferrofluid Dish | Ferrofluid | GPU Soft Disc | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 192 | 192 | 0.68 | 1.17 | YES | 485 | 0 | **PASS** |
| 82 | `ferrofluid-orb` | Levitated Ferrofluid Crown | Ferrofluid | GPU Soft Disc | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 192 | 192 | 0.46 | 1.31 | YES | 558 | 0 | **PASS** |
| 83 | `cracked-ice-sheet` | Movie Cracked Glacial Ice | Fracture | 3D Solid Shard | 640 | 0.00 | 1.40–3.80 | 0.65–1.65 | 512 | 512 | 0.00 | 3.39 | YES | 3187 | 0 | **PASS** |
| 84 | `ground-crack-quake` | Seismic Ground Crack Fissure | Fracture | 3D Solid Shard | 640 | 0.00 | 1.80–4.50 | 0.70–1.65 | 512 | 512 | 0.00 | 3.30 | YES | 4699 | 0 | **PASS** |
| 85 | `plasma-cage-arcs` | Plasma Cage & Arc Collector | Universal | GPU Spark Streak | 640 | 45.00 | 1.40–4.80 | 0.14–0.36 | 14 | 14 | 3.39 | 1.92 | YES | 564 | 0 | **PASS** |
| 86 | `lightning-web-mesh` | Dynamic Lightning Web & Storm Grid | Universal | GPU Spark Streak | 680 | 52.00 | 1.60–5.40 | 0.12–0.34 | 14 | 14 | 3.67 | 1.71 | YES | 449 | 0 | **PASS** |
| 87 | `kelp-forest` | Underwater Giant Kelp Forest | XPBD Strand | GPU Soft Disc | 240 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 51.88 | 2.05 | YES | 3464 | 0 | **PASS** |
| 88 | `sea-anemone` | Bioluminescent Sea Anemone Colony | XPBD Strand | GPU Soft Disc | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 35.84 | 4.62 | YES | 1375 | 0 | **PASS** |
| 89 | `beaded-door-curtain` | Crystal Bead Door Curtain | Universal | GPU Soft Disc | 280 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 280 | 280 | 0.95 | 3.56 | YES | 230 | 0 | **PASS** |
| 90 | `bamboo-wind-chimes` | Bamboo & Brass Wind Chimes | Universal | GPU Soft Disc | 120 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 120 | 120 | 0.34 | 2.22 | YES | 533 | 0 | **PASS** |
| 91 | `velvet-theater-drapes` | Velvet Theatre Drape Strips | Universal | GPU Soft Disc | 180 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 180 | 180 | 0.46 | 4.15 | YES | 3123 | 0 | **PASS** |
| 92 | `dandelion-puff` | Dandelion Puff & Wish Seeds | Universal | GPU Soft Disc | 460 | 0.00 | 0.60–2.20 | 12.00–18.00 | 460 | 460 | 3.90 | 6.46 | YES | 419 | 0 | **PASS** |
| 93 | `starling-murmuration` | Starling Murmuration & Predator Scatter | Boids Flock | 3D Winged Boid | 520 | 95.00 | 3.40–5.40 | 16.00–24.00 | 360 | 360 | 4.89 | 5.64 | YES | 718 | 0 | **PASS** |
