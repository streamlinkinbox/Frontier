# Frontier

## Vector Lab — female *Aedes aegypti* specimen rig (`mosquito/`)

Research-driven, real-time 3D hero mosquito for the fuel-drain game mechanic.
Open `mosquito/index.html` in a browser (serve the folder over HTTP, e.g.
`python3 -m http.server` inside `mosquito/`) — no build step, no CDN
(Three.js r160 is vendored).

- **Morphology** — procedural female *Aedes aegypti*: lyre scutum, pilose
  antennae, short white-tipped palps, banded tarsi, scaled wings + fringe,
  halteres, 8-segment articulated abdomen, claws + pulvilli + empodium.
- **Flight** — 40° stroke, ~560 Hz wingbeat, figure-8 deviation, advanced
  wing rotation, trailing legs, banked steering, stealth takeoff.
- **Walking** — wave→tetrapod→tripod gait continuum, analytic leg IK with
  joint limits, world-anchored stance feet, min-jerk swings, restricted
  workspaces (feet can never cross or snap).
- **Surfaces** — raycast attach on any uneven/smooth collider: terrain,
  deck, curved fuel tank (smooth → pulvilli), rock spire (rough → claws),
  vertical wall + ceiling.
- **Feeding** — full sequence: probe → fascicle insertion with buckling
  labium → salivation → cibarial pumping with abdomen engorgement → diuresis
  droplets → withdrawal → escape. Drains the player fuel tank or blood dish.
- Press **Field Guide** in-app for the research citations behind every rig
  parameter (Bomphrey/Muijres, Cruse/Walknet, Kong & Wu, …).
