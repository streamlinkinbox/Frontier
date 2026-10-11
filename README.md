# Frontier · Road Architect Studio

A high-fidelity, WebGPU-native procedural road network generator and civil asset authoring studio built for AAA game development and virtual world workflows.

Running live on modern **WebGPU** (with automatic WebGL2 fallback for universal browser compatibility), the application implements reference-scaled civil engineering road topology, pure quad-dominant surface meshing, seamless merged intersection geometry, a dual-tab **ProjectZeroEditor** UI with an interactive texture layerstack, precision AASHTO/Eurocode bridges, underpasses, railways with grade crossings and an animated train, speed bumps, potholes, construction rebuilding zones, and autonomous AI traffic.

---

## Key Technical Innovations

### 1. Pure Quad-Dominant Road & Intersection Topology
- **Quad Ribbon Sweeps**: Road carriageways, crown crossfalls (2% standard drainage), curbstone faces (150mm upstand with 15mm 45° chamfered top), gutters, and sidewalks are tessellated as continuous quad strips.
- **Merged Center Intersections**: Eliminates overlapping flat geometry (no z-fighting or ribbon stacking). When $N \ge 2$ road arms intersect (+, Y, T, or multi-leg star), each connected arm terminates at an approach mouth setback, and the central junction polygon is tessellated with **concentric quad loops** flowing smoothly into a central quad core.
- **Acute / Tight Angle Solver**: Unlike naive circular fillet solutions that project runaway tangents down the road on acute angles ($\Delta\theta < 45^\circ$), the civil solver clamps setbacks and constructs a smooth rounded nose splitter island with cubic Bézier transitions.
- **Elevation & Crown Harmonization**: Intersection vertices interpolate elevations from connected approach road crowns using inverse-distance harmonic weighting, ensuring C2 grade continuity and realistic stormwater drainage slopes.
- **Quad Wireframe Inspector**: Press <kbd>Z</kbd> or click the **Quad Wireframe** tool to inspect the quad topology cage in real time.

### 2. ProjectZeroEditor UI & Texture Layerstack
- Inspired by the Frontier ProjectZero / Slate dark studio theme: `#101010` shell, `#161616` panels, `#27272a` fine borders, and `DM Sans` / `IBM Plex Mono` typography.
- **Left Sidebar — Network Outliner**: Collapsible hierarchical tree grouping road corridors, intersection nodes, viaducts, tunnels, railways, and construction zones with live counts and visibility toggles.
- **Right Sidebar — Dual-Tab Inspector**:
  - **Entity Inspector Tab**: Live parametric controls for selected road or intersection (number of lanes, lane widths, crown crossfall %, curb height, sidewalk width, corner fillet radius, junction type).
  - **Texture Layerstack Tab**: Procedural PBR material layering system:
    1. *Base SMA Asphalt*: Dense Stone Matrix Asphalt with fine bitumen grain.
    2. *Aggregate Wear & Microtexture*: Exposed crushed stone specks.
    3. *Tire Wheelpaths & Skidmarks*: Longitudinal wheelpath polishing and approach braking skidmarks.
    4. *Moisture & Puddles*: Wet road sheen and localized reflective water depressions.
    5. *Cracks & Bitumen Tar Snakes*: Dark glossy crack sealant ribbons and fatigue cracks.
    6. *Sidewalk Paving Bond*: 8 real-world paving patterns (600×600 Granite Slabs, Herringbone 45° Brick, Herringbone 90° Brick, Historic Cobblestone, Hexagonal Pavers, Running Stretcher Bond, Dutch Clay, and ADA yellow blister tactile paving).
    - Each layer features toggleable visibility, blend mode, opacity, roughness, and normal mapping.

### 3. Civil Engineering & Realistic Infrastructure
- **Bridges & Viaducts (AASHTO / Eurocode Standards)**:
  - Modeled with strict **$\ge 5.5\text{m}$ vertical clearance** under deck so all commercial trucks clear.
  - Concrete deck slabs with 1.2m cantilevered overhang wings and drip grooves.
  - Structural steel plate I-girders or prestressed concrete box girders.
  - Pier bents with heavy crosshead beams (headstocks), elastomeric neoprene bearing pads, cylindrical columns, and spread footings.
  - **Collision Avoidance Solver**: Piers automatically test and avoid intersecting underlying roadways or railway alignments.
  - Concrete Jersey safety parapets with tubular pedestrian handrails and scuppers.
- **Underpasses & Underground Tunnels**:
  - Depressed roadway cutting with architectural retaining walls.
  - Reinforced concrete horseshoe / arch portal facade with clearance signage ("MAX CLEARANCE 5.5M / 18 FT").
  - Tunnel interior with continuous overhead LED linear light strips and ceiling jet ventilation fan units.
- **Railways & Grade Crossings**:
  - Crushed basalt stone ballast embankment with 1:1.5 side slopes.
  - Precast concrete sleepers (cross-ties) spaced at 0.60m with steel fastener clips.
  - Twin continuous welded steel rails (UIC 60 profile with mirror-polished running head).
  - At-grade crossing with heavy-duty rubber flangeway panels flush with asphalt.
  - Cantilever warning signal masts with crossbuck signs, alternating flashing red LED lights, and striped barrier gates.
  - **Animated Commuter Train**: Modern streamlined electric locomotive + passenger coach traveling smoothly along the track.
- **Road Damage & Reconstruction**:
  - *Speed Bumps*: 3 authentic types — parabolic asphalt hump with painted white chevrons, sinusoidal speed table with brick top, and modular yellow/black rubber speed cushion.
  - *Potholes*: Recessed cavity (55mm depth) with broken jagged asphalt lip and exposed crushed gravel base.
  - *Construction Zones*: Milled cold-planed grooved asphalt surface, fluorescent orange traffic cones with twin reflective collars, water-filled plastic Jersey barricades, and solar LED flashing arrow board trailer.
- **Precision Markings (AASHTO / MUTCD Compliant)**:
  - Continuous outer edge fog lines (0.15m).
  - Dashed lane lines (0.12m width, 3m stripe / 9m gap).
  - Double solid yellow centerline (two 0.12m stripes with 0.10m gap).
  - Stop bars (0.45m solid white line, **strictly on incoming traffic lanes**).
  - Pedestrian zebra crossings aligned with dropped curb ramps.
  - Directional turn arrow stencils (through, left, right, combo).
  - Bicycle lane stencils and painted green lanes.
- **Roadside Furniture & Autonomous Delivery Robots**:
  - Streetlights (Cobra-head highway, twin-arm boulevard, Victorian lantern) with active spot lights in Night mode.
  - Guardrails (W-beam with I-beam posts, Thrie-beam, Jersey barriers, tubular railings).
  - Bollards (stainless steel, cast-iron bell, illuminated LED, orange spring rebound).
  - Highway overhead gantry truss sign spanning lanes.
  - **Autonomous Sidewalk Delivery Robots**: 6-wheeled rovers with antenna flag and sensor visor roaming the sidewalks.
  - Paved City Blocks with pedestrian courtyards and cast-iron tree planter grates (no buildings!).
- **AI Traffic System ($\le 25$ Vehicles)**:
  - 5 realistic vehicle models: Executive Sedan, Compact SUV, Sports Coupe, Delivery Van, and City Transit Bus.
  - Intelligent Driver Model (IDM) car-following, lane tracking, wheel rotation, steering, deceleration brake lights, and nighttime headlights.
  - User-configurable density slider capped strictly at $\le 25$ vehicles.

### 4. Interactive 3D Move Gizmo
- Click any intersection node or road segment to inspect.
- The 3D translation gizmo allows moving intersections live in X, Y, and Z with real-time recalculation of connected road splines and concentric quad meshes.

---

## Pre-Configured Scenarios

1. **Metropolitan Interchange & Highway Bridge**: Complete diamond interchange with elevated 4-lane viaduct overpass, lower 4-lane arterial avenue, 4 directional on/off slip ramps, streetlights, overhead gantry sign, and cruising AI traffic fleet.
2. **Urban Boulevard & Rail Crossing**: 4-lane commercial boulevard intersecting a double-track railway at grade, active flashing crossbucks, rubber crossing panels, animated commuter train, speed bumps, potholes, and delivery robots.
3. **Highway Viaduct & Road Rebuilding**: High-level expressway viaduct crossing an underpass cutting, active resurfacing zone with milled asphalt, cones, barriers, and flashing arrow trailer.
4. **Pedestrian City Block & Plazas**: Enclosed urban perimeter with central paved pedestrian plazas, tree planter grates, illuminated bollards, and tactile paving (infrastructure only, no buildings).
5. **Acute Y-Junction (Quad Stress Test)**: Sharp 32° acute fork demonstrating the clamped setback and rounded nose splitter solver with pristine quad topology.

---

## Getting Started

### Development Server
```bash
npm install
npm run dev
```
Opens live preview on `http://0.0.0.0:3000`.

### Production Build & Standalone HTML
```bash
npm run build
```
Generates both:
- Optimized production bundle in `dist/`
- Completely self-contained, zero-dependency offline file `RoadArchitect.html` (1.3 MB).

### Exporting Assets
Click **Export GLB** or **OBJ** in the top navigation bar to download the 3D road network for use in Unreal Engine, Unity, Blender, or custom engines.
