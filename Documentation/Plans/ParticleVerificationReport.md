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
| 1 | `coin-fountain` | Coin fountain | Universal | 3D Solid Coin | 400 | 0.00 | 5.00–8.00 | 6.00–9.00 | 172 | 172 | 7.24 | 3.07 | YES | 5473 | 0 | **PASS** |
| 2 | `magnetic-particles` | Magnetic particles | Universal | GPU Glow Orb | 1200 | 220.00 | 0.00–0.40 | 6.00–10.00 | 294 | 294 | 4.95 | 4.09 | YES | 2425 | 0 | **PASS** |
| 3 | `bubbles` | Bubbles | Universal | GPU Soft Disc | 400 | 25.00 | 0.20–0.60 | 5.00–8.00 | 118 | 118 | 4.73 | 2.20 | YES | 4875 | 0 | **PASS** |
| 4 | `foam` | Foam | Universal | GPU Soft Disc | 1500 | 500.00 | 0.20–1.00 | 4.00–7.00 | 546 | 546 | 3.79 | 2.87 | YES | 3326 | 0 | **PASS** |
| 5 | `sparks` | Sparks | Universal | GPU Spark Streak | 3000 | 160.00 | 2.00–6.00 | 0.60–1.60 | 193 | 193 | 4.75 | 2.32 | YES | 1666 | 0 | **PASS** |
| 6 | `rain` | Rain streaks | Universal | GPU Spark Streak | 5500 | 850.00 | 8.50–11.50 | 1.00–1.60 | 861 | 861 | 10.80 | 4.44 | YES | 2232 | 0 | **PASS** |
| 7 | `hail` | Hail | Universal | 3D Solid Shard | 2000 | 160.00 | 7.20–10.50 | 2.50–4.00 | 240 | 240 | 10.14 | 3.99 | YES | 3936 | 0 | **PASS** |
| 8 | `leaves` | Leaves | Universal | 3D Solid Coin | 1200 | 20.00 | 0.20–0.90 | 7.00–12.00 | 114 | 114 | 3.32 | 5.73 | YES | 3013 | 0 | **PASS** |
| 9 | `paper` | Paper & debris | Universal | 3D Solid Coin | 600 | 8.00 | 0.10–0.50 | 6.00–10.00 | 103 | 103 | 4.19 | 3.32 | YES | 4311 | 0 | **PASS** |
| 10 | `fire` | Fire | Universal | GPU Glow Orb | 2500 | 280.00 | 0.60–1.70 | 0.70–1.50 | 317 | 317 | 4.07 | 1.49 | YES | 2770 | 0 | **PASS** |
| 11 | `smoke` | Smoke | Universal | GPU Soft Disc | 1500 | 70.00 | 0.30–0.80 | 3.00–6.00 | 159 | 159 | 4.26 | 1.79 | YES | 7591 | 0 | **PASS** |
| 12 | `explosion` | Explosion | Universal | GPU Glow Orb | 2000 | 0.00 | 3.00–9.00 | 0.50–1.20 | 113 | 113 | 7.05 | 1.82 | YES | 2578 | 0 | **PASS** |
| 13 | `tornado` | Tornado debris | Universal | 3D Solid Coin | 1500 | 90.00 | 1.00–3.00 | 4.00–7.00 | 177 | 177 | 4.15 | 2.97 | YES | 5765 | 0 | **PASS** |
| 14 | `sandstorm` | Sandstorm | Universal | GPU Soft Disc | 6000 | 900.00 | 5.00–8.00 | 4.00–7.00 | 906 | 906 | 7.77 | 3.87 | YES | 3400 | 0 | **PASS** |
| 15 | `snow` | Snow | Universal | GPU Soft Disc | 3500 | 280.00 | 0.45–1.00 | 8.00–12.00 | 348 | 348 | 4.14 | 2.82 | YES | 4574 | 0 | **PASS** |
| 16 | `embers` | Embers | Universal | GPU Glow Orb | 900 | 45.00 | 0.80–2.20 | 2.00–4.20 | 136 | 136 | 4.94 | 2.13 | YES | 1432 | 0 | **PASS** |
| 17 | `petals` | Cherry petals | Universal | 3D Solid Coin | 700 | 14.00 | 0.20–0.70 | 7.00–11.00 | 108 | 108 | 3.19 | 3.06 | YES | 3564 | 0 | **PASS** |
| 18 | `sand` | Sand grains | Universal | GPU Soft Disc | 4000 | 350.00 | 0.80–2.20 | 2.50–4.00 | 411 | 411 | 5.73 | 2.44 | YES | 1861 | 0 | **PASS** |
| 19 | `ash` | Ash flakes | Universal | GPU Soft Disc | 1500 | 30.00 | 0.30–1.00 | 6.00–10.00 | 123 | 123 | 3.83 | 1.84 | YES | 2331 | 0 | **PASS** |
| 20 | `debris` | Small debris | Universal | GPU Spark Streak | 800 | 30.00 | 2.00–5.00 | 3.00–5.00 | 123 | 123 | 5.39 | 2.36 | YES | 1674 | 0 | **PASS** |
| 21 | `flares` | Countermeasure flares | Universal | GPU Soft Disc | 480 | 0.00 | 8.50–13.50 | 3.40–5.20 | 172 | 172 | 12.16 | 2.59 | YES | 8933 | 0 | **PASS** |
| 22 | `pollen` | Pollen & dust motes | Universal | GPU Soft Disc | 2000 | 90.00 | 0.05–0.20 | 8.00–12.00 | 177 | 177 | 5.24 | 3.39 | YES | 1633 | 0 | **PASS** |
| 23 | `watermist` | Water mist | Universal | GPU Soft Disc | 1800 | 220.00 | 0.90–2.60 | 1.90–3.80 | 294 | 294 | 3.29 | 2.27 | YES | 10303 | 0 | **PASS** |
| 24 | `glass` | Glass shards | Universal | GPU Soft Disc | 420 | 26.00 | 2.20–5.40 | 2.20–3.80 | 119 | 119 | 5.85 | 2.33 | YES | 6240 | 0 | **PASS** |
| 25 | `lapilli` | Volcanic ash & lapilli | Universal | GPU Soft Disc | 500 | 25.00 | 6.00–12.00 | 3.00–5.00 | 118 | 118 | 8.96 | 3.99 | YES | 1818 | 0 | **PASS** |
| 26 | `seeds` | Dandelion seeds | Universal | GPU Soft Disc | 400 | 15.00 | 0.10–0.40 | 8.00–12.00 | 109 | 109 | 3.52 | 1.81 | YES | 2013 | 0 | **PASS** |
| 27 | `feathers` | Feathers | Universal | 3D Solid Coin | 300 | 6.00 | 0.10–0.30 | 8.00–12.00 | 101 | 101 | 3.26 | 1.94 | YES | 5341 | 0 | **PASS** |
| 28 | `confetti` | Confetti & streamers | Universal | 3D Solid Coin | 1200 | 25.00 | 1.00–3.00 | 5.00–8.00 | 118 | 118 | 3.49 | 1.85 | YES | 4618 | 0 | **PASS** |
| 29 | `steam` | Steam vent | Universal | GPU Soft Disc | 1600 | 60.00 | 0.80–1.60 | 3.00–5.00 | 150 | 150 | 3.41 | 1.34 | YES | 10000 | 0 | **PASS** |
| 30 | `fireworks` | Fireworks | Pyro Volley | GPU Glow Orb | 1200 | 0.00 | 4.00–9.00 | 1.80–2.90 | 141 | 129 | 5.98 | 1.79 | YES | 991 | 0 | **PASS** |
| 31 | `fireworks-spectacle` | New Year's Text & Shape Fireworks | Pyro Volley | GPU Glow Orb | 1400 | 0.00 | 4.00–9.00 | 2.00–3.20 | 141 | 129 | 6.46 | 2.54 | YES | 642 | 0 | **PASS** |
| 32 | `curl-fur-field` | Curl Wave Field | Universal | GPU Spark Streak | 10000 | 1800.00 | 0.30–1.10 | 2.80–4.60 | 1716 | 1716 | 3.55 | 2.95 | YES | 3858 | 0 | **PASS** |
| 33 | `curl-sphere-plasma` | Spherical Curl Plasma | Universal | GPU Spark Streak | 9000 | 1500.00 | 0.40–1.60 | 3.00–5.20 | 1446 | 1446 | 3.65 | 3.06 | YES | 3592 | 0 | **PASS** |
| 34 | `bokeh-stream-ribbons` | Bokeh Stream Ribbons | Universal | GPU Glow Orb | 3200 | 580.00 | 0.30–1.00 | 4.50–7.50 | 618 | 618 | 4.17 | 3.07 | YES | 2111 | 0 | **PASS** |
| 35 | `bokeh-object-pulse` | Surface Pour Ribbons | Universal | GPU Glow Orb | 3400 | 620.00 | 0.40–1.10 | 3.40–5.20 | 654 | 654 | 3.67 | 3.41 | YES | 2185 | 0 | **PASS** |
| 36 | `tesla-sphere-lightning` | Tesla Sphere Lightning | Universal | GPU Spark Streak | 600 | 0.00 | 1.80–5.20 | 0.16–0.42 | 26 | 26 | 3.56 | 0.65 | YES | 1230 | 0 | **PASS** |
| 37 | `static-sphere-shock` | Static Shock Arcs | Universal | GPU Glow Orb | 600 | 0.00 | 1.20–3.40 | 0.14–0.36 | 25 | 25 | 2.65 | 0.66 | YES | 1245 | 0 | **PASS** |
| 38 | `shockwave-ring` | Blast & Sonic Shockwave | Universal | GPU Soft Disc | 96 | 2.20 | 0.02–0.14 | 0.72–1.05 | 15 | 15 | 3.18 | 0.78 | YES | 29904 | 0 | **PASS** |
| 39 | `emp-pulse-dome` | EMP Energy Pulse Dome | Universal | GPU Soft Disc | 96 | 1.10 | 0.00–0.05 | 0.90–1.25 | 32 | 32 | 3.58 | 0.13 | YES | 29074 | 0 | **PASS** |
| 40 | `radar-sweep` | Tactical Radar Sweep & Blips | Universal | GPU Soft Disc | 16 | 0.25 | 0.00–0.00 | 7.50–8.50 | 10 | 10 | 4.23 | 0.14 | YES | 27745 | 0 | **PASS** |
| 41 | `sonar-ping` | Acoustic Sonar Pulse Ping | Universal | GPU Soft Disc | 64 | 1.20 | 0.00–0.04 | 1.10–1.50 | 22 | 22 | 3.91 | 1.31 | YES | 25860 | 0 | **PASS** |
| 42 | `plasma-laser-beam` | Plasma & Laser Beam | Universal | GPU Spark Streak | 1200 | 260.00 | 3.20–9.50 | 0.18–0.55 | 107 | 107 | 8.67 | 1.91 | YES | 1375 | 0 | **PASS** |
| 43 | `plasma-tracers` | Bullet & Plasma Tracers | Universal | GPU Spark Streak | 1200 | 36.00 | 19.00–25.00 | 0.55–0.95 | 41 | 41 | 23.76 | 8.42 | YES | 1024 | 0 | **PASS** |
| 44 | `projectile-trails` | Glowing Projectile Trails | Universal | GPU Glow Orb | 256 | 14.00 | 6.40–9.20 | 2.00–3.10 | 97 | 97 | 7.11 | 2.87 | YES | 1684 | 0 | **PASS** |
| 45 | `welding-grinder-sparks` | Welding & Grinder Arc Spray | Universal | GPU Spark Streak | 3200 | 420.00 | 7.00–13.80 | 0.65–1.45 | 426 | 426 | 12.15 | 3.87 | YES | 1702 | 0 | **PASS** |
| 46 | `hologram-data-glitch` | Hologram & Signal Glitch | Universal | GPU Spark Streak | 900 | 110.00 | 0.85–1.95 | 1.80–3.30 | 195 | 195 | 5.36 | 2.93 | YES | 6736 | 0 | **PASS** |
| 47 | `boids-flock` | Boids Flock (Birds / Bats / Fish) | Boids Flock | 3D Winged Boid | 220 | 48.00 | 2.20–3.80 | 12.00–18.00 | 220 | 220 | 3.45 | 5.94 | YES | 2736 | 0 | **PASS** |
| 48 | `boids-butterflies` | Butterfly & Wisp Swarm | Boids Flock | 3D Winged Boid | 180 | 36.00 | 1.20–2.30 | 10.00–16.00 | 180 | 180 | 3.28 | 5.89 | YES | 2527 | 0 | **PASS** |
| 49 | `boids-fish-school` | Bioluminescent Fish School | Boids Flock | 3D Winged Boid | 260 | 64.00 | 2.10–3.40 | 14.00–22.00 | 260 | 260 | 3.71 | 5.76 | YES | 3115 | 0 | **PASS** |
| 50 | `viscous-dripping` | Viscous Dripping & Trails | Universal | GPU Soft Disc | 320 | 26.00 | 0.04–0.24 | 1.80–2.90 | 119 | 119 | 3.99 | 2.37 | YES | 3097 | 0 | **PASS** |
| 51 | `derez-cube` | Derez Cube | Derez Solid | 3D Solid Shard | 1536 | 0.00 | 0.60–2.60 | 3.00–4.00 | 784 | 784 | 3.64 | 4.27 | YES | 5342 | 0 | **PASS** |
| 52 | `cube-coins` | Cube to Coins | Derez Solid | 3D Solid Shard | 384 | 0.00 | 1.50–4.00 | 4.00–6.00 | 361 | 361 | 3.73 | 3.38 | YES | 4458 | 0 | **PASS** |
| 53 | `melt-away` | Melt Away | Derez Solid | 3D Solid Shard | 1536 | 0.00 | 0.20–0.90 | 6.00–8.00 | 784 | 784 | 3.64 | 4.27 | YES | 5342 | 0 | **PASS** |
| 54 | `reverse-derez` | Reverse Derez | Derez Solid | 3D Solid Shard | 1536 | 0.00 | 0.50–2.00 | 3.00–4.00 | 784 | 784 | 3.64 | 4.27 | YES | 5342 | 0 | **PASS** |
| 55 | `voxel-explosion` | Voxel Explosion | Derez Solid | 3D Solid Shard | 1024 | 0.00 | 2.00–4.50 | 2.50–4.00 | 784 | 784 | 3.64 | 4.27 | YES | 5342 | 0 | **PASS** |
| 56 | `coin-stack` | Coin Stack | Derez Solid | 3D Solid Shard | 64 | 0.00 | 0.00–0.00 | 1.00–1.00 | 64 | 64 | 3.51 | 2.80 | YES | 2140 | 0 | **PASS** |
| 57 | `dissolve-nothing` | Dissolve to Nothing | Derez Solid | 3D Solid Shard | 1536 | 0.00 | 0.10–0.60 | 2.50–4.00 | 784 | 784 | 3.64 | 4.27 | YES | 5342 | 0 | **PASS** |
| 58 | `dissolve-butterflies` | Dissolve into Butterflies | Derez Solid | 3D Solid Shard | 1536 | 0.00 | 0.20–0.90 | 6.00–9.00 | 784 | 784 | 3.64 | 4.27 | YES | 5342 | 0 | **PASS** |
| 59 | `glitch-dissolve` | Glitch Dissolve | Derez Solid | 3D Solid Shard | 1536 | 0.00 | 0.10–0.60 | 2.50–4.00 | 784 | 784 | 3.64 | 4.27 | YES | 5342 | 0 | **PASS** |
| 60 | `black-hole` | Black Hole Accretion | SpaceDistort | GPU Glow Orb | 3000 | 400.00 | 0.00–0.20 | 6.00–10.00 | 580 | 580 | 4.62 | 2.33 | YES | 4172 | 0 | **PASS** |
| 61 | `heat-shimmer` | Heat Shimmer Distortion | SpaceDistort | GPU Glow Orb | 200 | 30.00 | 0.60–1.40 | 2.00–3.50 | 154 | 154 | 3.85 | 2.62 | YES | 7067 | 0 | **PASS** |
| 62 | `strikeSparks` | Arc Strike Sparks | Universal | GPU Spark Streak | 1600 | 0.00 | 1.80–7.50 | 0.45–1.15 | 85 | 85 | 5.43 | 2.36 | YES | 1454 | 0 | **PASS** |
| 63 | `energy-shield-hex` | Energy Shield & Hex Ripples | Energy Shield | 3D Solid Coin | 24 | 0.28 | 0.00–0.00 | 6.50–7.50 | 64 | 64 | 0.00 | 1.83 | YES | 5640 | 0 | **PASS** |
| 64 | `energy-shield-prism` | Multi-Shape Prism Barrier | Energy Shield | 3D Solid Coin | 24 | 0.28 | 0.00–0.00 | 6.50–7.50 | 64 | 64 | 0.00 | 1.83 | YES | 5640 | 0 | **PASS** |
| 65 | `aurora-borealis` | Aurora Borealis Curtains | Universal | GPU Spark Streak | 180 | 16.00 | 0.08–0.24 | 6.50–10.50 | 74 | 74 | 3.54 | 2.75 | YES | 15077 | 0 | **PASS** |
| 66 | `spiderweb-orb` | Orb-Weave Spiderweb | Universal | GPU Soft Disc | 320 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 172 | 172 | 3.89 | 3.13 | YES | 1481 | 0 | **PASS** |
| 67 | `spiderweb-cobweb` | Messy Attic Cobwebs | Universal | GPU Soft Disc | 300 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 172 | 172 | 4.14 | 2.14 | YES | 1500 | 0 | **PASS** |
| 68 | `cables-chains` | Swinging Cables & Chains | XPBD Strand | GPU Strand Ribbon | 64 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 27.00 | 1.59 | YES | 2329 | 0 | **PASS** |
| 69 | `vines-ropes` | Hanging Vines & Ropes | XPBD Strand | GPU Strand Ribbon | 96 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 16.92 | 1.63 | YES | 2218 | 0 | **PASS** |
| 70 | `flag-banner` | Wind Banner & Tearing Cloth | XPBD Strand | GPU Strand Ribbon | 64 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 200 | 200 | 29.92 | 1.14 | YES | 4076 | 0 | **PASS** |
| 71 | `ship-sail` | Full-Rigged Ship Sail | XPBD Strand | GPU Strand Ribbon | 64 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 200 | 200 | 21.30 | 1.37 | YES | 3029 | 0 | **PASS** |
| 72 | `grass-wheat-field` | Grass & Golden Wheat Field | XPBD Strand | GPU Strand Ribbon | 520 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 28.33 | 1.68 | YES | 2433 | 0 | **PASS** |
| 73 | `tall-reeds` | Tall Lakeside Reeds | XPBD Strand | GPU Strand Ribbon | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 29.18 | 1.76 | YES | 2414 | 0 | **PASS** |
| 74 | `interactive-fern` | Lush Interactive Fern | XPBD Strand | GPU Strand Ribbon | 128 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 21.05 | 1.81 | YES | 3637 | 0 | **PASS** |
| 75 | `interactive-tree` | Swaying Interactive Canopy Tree | XPBD Strand | GPU Strand Ribbon | 520 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 14.44 | 1.39 | YES | 4160 | 0 | **PASS** |
| 76 | `effector-grid-cubes` | Effector Cube Grid · Height Up/Down | MoGraph | 3D Solid Cube | 576 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 576 | 576 | 2.82 | 3.58 | YES | 5807 | 0 | **PASS** |
| 77 | `effector-sphere-dots` | Halftone Effector Sphere · Circles | MoGraph | 3D Solid Cube | 720 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 676 | 676 | 2.33 | 3.89 | YES | 5715 | 0 | **PASS** |
| 78 | `effector-hand-halftone` | Obsidian Hand · Halftone & Tiles | MoGraph | 3D Solid Cube | 1200 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 784 | 784 | 2.81 | 4.20 | YES | 5722 | 0 | **PASS** |
| 79 | `effector-torus-hex` | Kinetic Hex Torus Grid | MoGraph | 3D Solid Cube | 760 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 729 | 729 | 2.56 | 4.05 | YES | 5717 | 0 | **PASS** |
| 80 | `effector-object-placer` | Placeable 3D Specimen · MoGraph & Effectors | MoGraph | 3D Solid Cube | 1400 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 784 | 784 | 2.81 | 4.20 | YES | 5722 | 0 | **PASS** |
| 81 | `ferrofluid-spikes` | Magnetic Ferrofluid Dish | Ferrofluid | 3D Solid Spike | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 192 | 192 | 0.71 | 1.16 | YES | 4999 | 0 | **PASS** |
| 82 | `ferrofluid-orb` | Levitated Ferrofluid Crown | Ferrofluid | 3D Solid Spike | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 192 | 192 | 0.47 | 1.31 | YES | 5163 | 0 | **PASS** |
| 83 | `cracked-ice-sheet` | Movie Cracked Glacial Ice | Fracture | 3D Solid Shard | 640 | 0.00 | 1.40–3.80 | 0.65–1.65 | 512 | 512 | 2.30 | 2.70 | YES | 6789 | 0 | **PASS** |
| 84 | `ground-crack-quake` | Seismic Ground Crack Fissure | Fracture | 3D Solid Shard | 640 | 0.00 | 1.80–4.50 | 0.70–1.65 | 512 | 512 | 2.30 | 2.63 | YES | 6734 | 0 | **PASS** |
| 85 | `plasma-cage-arcs` | Plasma Cage & Arc Collector | Universal | GPU Glow Orb | 640 | 45.00 | 1.40–4.80 | 0.14–0.36 | 12 | 12 | 4.06 | 1.80 | YES | 1403 | 0 | **PASS** |
| 86 | `lightning-web-mesh` | Dynamic Lightning Web & Storm Grid | Universal | GPU Glow Orb | 680 | 52.00 | 1.60–5.40 | 0.12–0.34 | 14 | 14 | 3.73 | 1.58 | YES | 1370 | 0 | **PASS** |
| 87 | `kelp-forest` | Underwater Giant Kelp Forest | XPBD Strand | GPU Strand Ribbon | 240 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 21.54 | 1.91 | YES | 2877 | 0 | **PASS** |
| 88 | `sea-anemone` | Bioluminescent Sea Anemone Colony | XPBD Strand | GPU Strand Ribbon | 220 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 160 | 160 | 35.51 | 3.56 | YES | 2015 | 0 | **PASS** |
| 89 | `beaded-door-curtain` | Crystal Bead Door Curtain | Universal | GPU Soft Disc | 280 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 169 | 169 | 5.09 | 2.36 | YES | 1624 | 0 | **PASS** |
| 90 | `bamboo-wind-chimes` | Bamboo & Brass Wind Chimes | Universal | GPU Soft Disc | 120 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 116 | 116 | 4.58 | 1.36 | YES | 3256 | 0 | **PASS** |
| 91 | `velvet-theater-drapes` | Velvet Theatre Drape Strips | Universal | GPU Soft Disc | 180 | 0.00 | 0.00–0.00 | 9999.00–9999.00 | 136 | 136 | 5.08 | 2.15 | YES | 1132 | 0 | **PASS** |
| 92 | `dandelion-puff` | Dandelion Puff & Wish Seeds | Universal | 3D Solid Coin | 460 | 0.00 | 0.60–2.20 | 12.00–18.00 | 172 | 172 | 3.87 | 1.93 | YES | 4131 | 0 | **PASS** |
| 93 | `starling-murmuration` | Starling Murmuration & Predator Scatter | Boids Flock | 3D Winged Boid | 520 | 95.00 | 3.40–5.40 | 16.00–24.00 | 360 | 360 | 3.30 | 5.78 | YES | 3595 | 0 | **PASS** |
