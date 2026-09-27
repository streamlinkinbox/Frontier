# Frontier

Static web app — open `app/index.html` (lobby) or `app/eagle/index.html` (Cliff Eagle scene).
No build step: everything is plain ES modules; Three.js is vendored in `app/vendor/`.

```bash
python3 -m http.server 8000 --directory app   # then open http://localhost:8000/eagle/
```

## Cliff Eagle (`app/eagle/`)

A canyon road below a cliff. A giant bald eagle perches on a rock ledge ~45 m above the road;
when a car passes it launches, stoops, flares, grabs the car with its talons, climbs out over the
valley, dives at the cliff face and hurls the car into the rock. Then it circles back and lands.

| file | what it is |
| --- | --- |
| `terrain.js` | analytic height field (cliff wall, perch ledge, canyon road, valley) + mesh with triplanar rock/ground shading, road ribbon, guard rail, boulders, scrub |
| `car.js` | rigid-body vehicle: 4 hover/ray suspension points, simplified **Pacejka** magic-formula tyres (lateral + longitudinal, friction-ellipse combined), AWD, brakes, handbrake, aero, body-vs-terrain contacts, crumple deformation, detachable wheels; lofted coupe mesh |
| `eagle.js` | the eagle: articulated skeleton (shoulder/elbow/wrist per wing, 3-segment legs, 4 toes + talons, 2-segment neck), ~320 individually posed & aerodynamically bent feathers merged into 6 draw calls, procedural plumage / scales / eye / beak; `pose()` turns a small parameter set into biomechanically consistent joint angles |
| `eagleBrain.js` | flight controller (bounded-acceleration steering → bank / pitch / wing power) and the hunting state machine: PERCH → ALERT → LAUNCH → STOOP → APPROACH → FLARE → GRAB → LIFT → SLAM_RUN → PULL_UP → RETURN → LAND |
| `textures.js` | canvas painters: flight feathers (rachis, barbs, splits, downy base, asymmetric vanes), contour plumage, scaly skin, sandstone, ground, asphalt, eye, sprites; height → normal maps |
| `fx.js` | dust / smoke / sparks particle systems, rock, glass and metal debris |
| `sky.js` | sky dome shader + PMREM environment |
| `audio.js` | procedural WebAudio: engine, wind, wing beats, screech, impacts |
| `main.js` | scene, lighting/shadows, input, traffic, camera director (auto cinematic shots / chase / eagle / orbit), HUD |

Controls: `W/S` throttle & brake/reverse, `A/D` steer, `Space` handbrake, `C` camera mode, `R` respawn, `H` hide help, drag to orbit.
