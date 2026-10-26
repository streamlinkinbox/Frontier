# Frontier Editor — Terrain generator (experimental, browser)

Procedural terrain generator (mountains, cliffs, river plains, dunes) in the Frontier Editor UI (outliner · viewport ·
inspector cards). It is a plain static page: no bundler, no JSX, Three.js resolves from a CDN
import map, so it runs straight from GitHub via raw.githack, GitHub Pages or any static server.

```
https://raw.githack.com/<owner>/Slate/<branch>/Frontier/Experimental/CliffGenerator/index.html
```

Local preview:

```sh
cd Frontier/Experimental/CliffGenerator
python3 -m http.server 5173 --bind 0.0.0.0     # or: npm install && npm run dev
```

## Representation

The terrain is **heightfield‑based**: a 2‑D grid of heights (128²–2048²) is what the noise,
strata, erosion and river simulation operate on, and what the heightmap / satmap / mask exports
describe. The *rendered mesh* goes further: steep parts are replaced by **true‑3D SDF chunks**
(`src/sdf-chunks.js`, meshed in `src/sdf.worker.js`) — a 3‑D field made of the heightfield
distance plus strata carving (soft beds undercut, hard beds left as lips), polygonised with
marching cubes. The chunk field fades to the plain heightfield over a blend margin and is exactly
`y − h` on every face shared with the heightfield mesh, so iso‑crossings sit on the heightfield's
own border polyline: **no seams, no stitching**. Elsewhere the heightfield mesh is displaced
horizontally (overhangs) and along the normal (detail). The chunks give real undercuts,
overhangs, notches and shelters; caves and arches are possible wherever the carve breaks
through. The isolated proof of the technique lives in `../SdfCliffLab`.

## Pipeline — base shape → noise → erosion → rocks → surface

1. **Base relief** (`src/heightfield.js`) — domain‑warped ridged multifractal with a continental
   mask, optional mesa soft‑clamp and a meandering canyon incision. **Dunes**: transverse ridges
   across the wind with a concave windward slope steepening to a sharp brink, a straight slip
   face at the angle of repose, interdune flats and smaller compound dunes climbing the big ones.
   **Rugged outcrops** (`src/rugged.js`, Gaea *Rugged / Outcrops*‑style) — a heightfield can only
   move up and down, so the steep faces of the base shape are also pushed and pulled **sideways**:
   every steep cell re‑samples the terrain from a point displaced along the face's own horizontal
   normal by a blocky multi‑octave noise (3‑D coarse blocks turned into plateaus and risers, finer
   blocks on top), so buttresses stand proud of the face where the noise is positive and recesses
   / chimneys are cut back where it is negative, with a vertical share so the blocks also step
   up and down as ledges. The slope mask is blurred so a whole buttress moves as one piece and
   flat ground (valleys, rivers, plains) is never displaced. Applied to the base shape and again,
   finer, after erosion (which smears lateral structure) — the hardness and boulder maps ride along
   with the faces. Controls: push–pull distance, block scale, blockiness, levels, ledges, slope
   threshold, after‑erosion share. The 3‑D cliff chunks carry the same idea into true 3‑D
   (*Cliff depth → 3D push–pull*): a blocky noise that varies with height pushes buttresses out
   of the carved face and cuts recesses back into it, so a block can overhang the recess below it
   — real XZ push–pull with overhangs, not just contour wiggles. **Escarpment** (*Landform →
   Mesa & canyon → Escarpment*): a near‑vertical cliff line along the edge of the massif — the
   high side is lifted onto a bench within a cell or two along a smoothed version of the
   continental mask (long headlands and bays), with optional *Stacks* — pillars of the former cliff left standing just off the line — then pushed and pulled in plan by the rugged stage;
   this is the base of the *Coastal cliffs* and *Quarry walls* presets. In the 3‑D chunks
   (*Cliff depth → True‑3D cliffs*) **Vertical wall** shears such a face in the SDF: the height
   sample of every point is taken at a plan position shifted along the local uphill direction by
   (t − ½)·W·vertical, t = position between the local foot and crest, W = plan width of the
   face in the heightfield (crest − foot over the face slope) — so the crest moves out, the foot
   moves in, and at 1 a back‑leaning / slope becomes a | wall, above 1 it overhangs ( \ / ). The
   strata are still looked up at the true position, so the beds stay horizontal across the sheared
   wall, and the shear fades to nothing where the chunk weight fades, so the seam to the heightfield
   is untouched. The band is wider (the whole foot…crest column is polygonised) so it costs more
   voxels. *Lean* additionally cuts the face back progressively from crest to foot (undercut).
   **Rock blocks** (*Cliff depth → True‑3D cliffs → Rock blocks / Rock block size / Missing
   blocks*) turn the face into a wall of discrete rock blocks in the SDF itself: the bed rows of
   the strata model × two vertical joint families (world‑aligned planes, resized and staggered
   bed to bed like brickwork). Every block stands proud of or sits back from the face by its own
   amount, with its own slight tilt and its own rounding of the rim, every block boundary is a
   crack groove and a share of the blocks have fallen out, leaving deep recesses; soft beds
   weather smooth, hard beds break into blocks; a joint family parallel to the face is blended
   away (it would only show as spurious lines). This is geometry — rocks and cracks you can look
   along, not a shading trick — so blocks have to be at least ~3 voxels wide: the cliff presets use
   a 1 km world (1 m voxels at 512²); raise *Voxel budget* or lower the world size for finer
   blocks. **Cliff protection** (*Erosion → Fluvial incision → Cliff protection*): faces steeper
   than the protection angle keep their pre‑erosion shape through every erosion stage, so the
   plateau above a sea cliff or quarry wall can be eroded into a real landscape — valleys, rills,
   streams that simply hang at the crest — while the wall stays a wall.
   **Rock stacks** (*Landform → Rock stacks (Gaea Stacks)*): tiered rock towers in the manner of
   Gaea's Stacks node — a mask (a smooth union of warped discs around jittered feature points) is
   thresholded at a rising level per tier, so every tier sits inside the one below; each tier has
   its own thickness, edge width and, under *Chaos*, its own plan offset and outline wobble, so
   the pile leans and steps irregularly instead of a wedding cake. *Spires* shrinks the cells and
   discs towards isolated hoodoos, *Taper* goes from straight towers to stepped pyramids, a talus
   *Pedestal* ramps up to the lowest tier. Three controls keep the *scatter* organic instead of a grid: *Size spread* draws
   size and height from a heavy‑tailed distribution (a few anchor towers among many small ones), *Elongation*
   stretches each stack's plan along its own direction into ridges and fins, *Clustering* lets a cell hold a tight
   group of stacks and leaves others bare, so neighbours touch and fuse into compounds — and every stack gets its
   own height, taper, outline roundness and lean direction, with the whole mask warped at two scales. Two experimental presets: *Desert stacks* (red sandstone
   buttes and spires under normal erosion — fluted walls, talus skirts) and *Stack cliffs* (the
   same towers treated like the quarry / coastal walls: cliff protection, vertical sheared walls,
   rock blocks). **Joint‑set domains** keep the walls from reading as one pattern: the face is split into coarse
   panels and each panel gets its own block size, block depth and grid phase, the way a real cliff shows patches of
   massive slabs beside closely jointed ones instead of one wallpaper over every wall in the world. **Jointing** (*Jointing*, shader): two vertical joint sets (families
   of warped planes spaced like the bed thickness, staggered bed to bed like brickwork) and the
   bedding planes break the face into blocks — each joint with its own width, some missing, each
   block with its own slight tilt and tone, grooves with normal and occlusion — the cracked, blocky
   rock face of a quarry wall or a sea cliff; a joint family parallel to the face is invisible.
   **Breaking the tile.** A cliff that reads as a repeated tile is almost never a geometry problem:
   the eye catches a *lattice* of equal rectangles with the same finish everywhere. Five controls act
   on it together, and they move the texture and the 3‑D geometry at once so the two never disagree:
   *Joint swing* (shader) and *Block swing* (true‑3D) rotate the joint and block frames slowly across
   the map, the way joint sets curve around a dome, so no wall stays square to the view; *Block size
   varies* hashes the same coarse domain grid the carved blocks use, so one panel is massive slabs and
   the next is closely jointed; *Pattern warp* stretches and shifts the cells of every non‑bedding
   pattern (joints, spalls, flakes, gravel) so nothing marches along the world axes; *Detail
   patchiness* varies how much weathering each patch carries at all. *Strata → Lateral change* goes to
   0.8 now (default 0.26): bed boundaries thicken, thin, rise and dip across the map, instead of every
   cliff in the world showing the same rhythm at the same heights.
   **Subdivision.** The mesh can be refined well past the simulated grid — *Mesh subdivision* now
   reaches ×6 with the mesh side capped at 4097 vertices instead of 2049, and ×3 (1534²) is the
   default: at a 512² grid that is 4.7 M triangles instead of 2.1 M, which is what the displacement
   detail (knobs, buttresses, rocky facets — pushed per vertex) was waiting for. The true‑3D cliffs
   gain a 4th voxel level (voxels a quarter of a grid cell) and the auto budget defaults to 36 M.
   **Boulder outcrops** (`src/outcrops.js`) — clusters of large core‑stones (tors, woolsacks,
   "encampments") are unioned into the relief *before* strata and erosion: superellipsoid stones
   with a plan rotation, a low‑frequency bulge and a burial depth, in clusters on a site grid
   filtered by slope and biased to ridges / hill tops. They come out as a landform the rest of the
   pipeline weathers — pinned hard, no bedding terraces, no scree skin, no bedding undercuts in
   the 3‑D chunks — and distinct from the scattered rock props.
2. **Strata** (`src/strata-model.js`, Gaea *Stratify*‑style) — one stratigraphic column is shared
   by the terracing, the erosion hardness, the 3‑D cliff carving and the shader, so the beds you
   see are the beds that were carved. The column is a sequence of *packages*: thin‑bedded (mostly
   soft, the odd hard ledge), massive (mostly hard, the odd soft parting) and mixed, each bed with
   its own log‑normal thickness, hardness, tint and optional gradational base; beds thicken and
   thin laterally (an expression evaluated identically in JS and GLSL). The heightfield is terraced
   in the tilted (dip) frame — thin beds step less — and hardness is kept as a map that drives
   everything downstream. In the 3‑D chunks soft beds recede into a notch deepest just under the
   caprock (thin partings a groove, thick soft beds a deep undercut), hard beds break along joint
   sets spaced like their thickness into blocky columns with the occasional fallen block, and the
   shader paints each bed from the same table (hard = pale/clean, soft = darker/warmer, plus the
   bed's tint, laminae and seams).
3. **Fluvial incision** (`src/fluvial.js`) — the stream‑power model ∂h/∂t = U − K·A^m·S + D∇²h
   that gives real terrain its dendritic valleys. Rivers cut down in proportion to the water they
   gather (drainage area A) and their slope, resistant beds lower K (knickpoints, benches),
   hillslopes diffuse towards the channels and the massifs are uplifted a little so the relief
   stays high while the valleys deepen. Solved with the Braun–Willett implicit scheme (cells
   processed from the outlets upwards, closed form per cell, unconditionally stable) over a few
   dozen long steps, re‑routing the water (priority‑flood fill + D8) every step. **Sedimentation**:
   what the rivers cut is carried downstream (donors before receivers) and dropped wherever the
   flux exceeds the transport capacity ∝ A^m·S — alluvial fans where slopes flatten, valley
   fills, deltas and basin floors; the deposited thickness becomes an *alluvium* map: the
   surface paints it as silt / sand with some gravel, and the rivers widen and braid on it
   (shifting bars in a gravel belt) the way a loaded river does on its own fan. Closed basins
   silt up towards their spill level (*Basin fill*) so they become valley floors drained by a
   river — standing‑water lakes are off by default (*Lakes in depressions* turns them back on); after the droplet erosion, hollows shallower than
   *Silt up pits* are filled so the sediment fans do not dam the valleys into chains of ponds.
   The simulated rivers then run in valleys they shaped themselves instead of being carved into an
   unrelated surface. After the droplet erosion a **rill** pass (same solver, low area exponent,
   noisy routing, no fill) cuts the fine converging flow lines that cover eroded slopes — the
   flow texture of Gaea's Erosion node — and its drainage area is the wet‑line flow map; the
   strata are then **re‑cut** (lighter) on the eroded surface so the ledges stay crisp.
4. **Erosion** (`src/erosion.js`) — thermal pre‑settle, Lagrangian droplet hydraulic erosion
   (cutting scaled by hardness so cliff bands survive, deposition builds fans), then talus
   slumping with a hardness‑dependent repose angle → scree aprons beneath the faces.
   Outputs: height, deposit, flow (log accumulation), hardness, cavity, slope.
   **Rivers** (`src/hydrology.js`, Gaea‑style): priority‑flood depression filling (sea and tile
   border as outlets), D8 flow routing and accumulation on the eroded surface, channels where the
   catchment exceeds a threshold with width ∝ √catchment, a monotone bed / water profile from the
   outlets upstream, concave beds with sloped banks, braided gravel bars on wide gentle reaches,
   and lakes where basins fill. The channel is cut around a **smoothed centreline**: the D8 path
   is a staircase of cell centres, so each channel cell's position is averaged along its stream
   (main donor ← cell → receivers, stopping where it joins a much bigger river) and the bed is
   rasterised segment by segment as distance to that polyline — meanders are smooth curves,
   junctions are owned by the trunk, and a dry gully can never punch a hole in the water beside
   its mouth. Channels are narrow (≈ 22 m per √km² by default, capped at 80 m); the braid belt
   only widens a loaded reach by half. Beside a stream of any size a **floodplain** (*Floodplain*,
   in channel widths) planes ground that is not much higher than the water down to a gently
   rising flat — the river valley of a mature stream — while mountainsides above it are left
   alone. Water that is flowing stays wet downstream; only the ephemeral headwater gullies are
   dry. **Water is painted, not meshed, by default** (*Water as meshes* off, Gaea‑style): the
   surface shader colours everything under the water line with the river colour (clear tint over
   the bed in the shallows, full colour where it is deep), flattens the normal and lowers the
   roughness, so the terrain stays dry geometry with the water as a mask — exactly what the
   mask/satmap exports carry. The painted level is a *continuous* per‑vertex field (extrapolated
   outwards from the wet cells, falling away with distance) rather than the simulation's dry
   sentinel, so the shoreline is a clean line through the triangles instead of a cell‑by‑cell
   checkerboard. Turning *Water as meshes* on brings back the sea plane and the river / lake
   sheets. Painted water also follows the *Sea* switch: with it off the terrain stays dry even where a
   basin closed or a lake was drawn, so a desert landform cannot inherit water from an earlier wet setting. **Bank gullying** (*Rivers & lakes → Simulated rivers → Bank gullying*): after the channels are cut, runoff off
   the valley sides converges on the river and erodes small gullies into the soft bank material
   (fine‑scale rills masked to the banks, never below the water line) — the soil erosion a river
   valley shows on its flanks. Lakes are where basins fill (partial fill, area cap — the
   biggest/deepest basins first; over the cap a river that ends in a hollow keeps a small pond).
   Lakes are judged on the real surface, only basins large *and* deep enough qualify, the basin
   floor and the rim outside the water line are smoothed (sediment floor, wave‑worked shore) and
   the water area is morphologically opened, so the outline is a clean curve rather than the
   cell‑by‑cell contour of droplet deposits. River and lake water has its own material
   (`src/water-material.js`): per‑vertex depth makes the shallows clear (bed and gravel bars show
   through) and only deeper water takes the water colour, so shores fade in instead of ending in
   a hard dark edge; where the water surface drops steeply (rapids, falls) it turns to white water.
   The bed and water of the nearest channel cell are interpolated along the channel direction
   and the floor smoothed, so a steep river is a ramp, not a staircase of treads. Reaches steeper
   than *Dry above grade* show the carved gully (wet rock, gravel) with no standing water — a
   mountain torrent does not read as flat water — unless the river is big; lake shores are eased
   into a shelving beach. Steep reaches are wide shallow V gullies; inside closed depressions water runs down the
   real floor into the lake instead of in straight lines across the filled flat; a little noise on
   the routing surface breaks the dead‑straight D8 lines smooth slopes produce. The water mesh is
   one continuous sheet with per‑vertex levels (relaxed along the channel) so it follows the
   river's grade. Drawn rivers are *guides*: carved first and injected as flow so the network
   passes through them. Flow and deposit maps pick the network up for shading (wet gullies, gravel
   beds).
5. **Mesh** (`src/terrain-geometry.js`) — indexed grid with alternating diagonals, per‑vertex
   normals and an `aux` attribute `(deposit, flow, hardness, cavity)`; a skirt turns the tile
   into a cut block of ground. **Cliff depth**: steep vertices are displaced horizontally along
   the face normal — hard beds out, soft beds in, plus buttress/alcove noise — so the mesh has
   genuine overhangs, ledges and recesses that the heightfield itself cannot represent.
   **Mesh detail**: the mesh can carry 1–4× the heightfield's vertices (bicubic upsample, side
   capped at 2049) plus fine relief pushed along the surface normal (knobs on steep rock,
   hummocks on flat ground) and **rocky facets** (Gaea *Rocky*‑style: a cellular field of
   joint‑bounded blocks, wider than tall, pushed out of steep *hard* rock while soft beds stay
   smooth; blockiness blends cones into sharp‑edged blocks) and **crags** (larger joint‑bounded
   blocks, each protruding or set back by its own amount, so a face is a stack of rugged masses
   rather than a plane) — cheap detail on top of the expensive erosion. The same detail functions
   (`src/detail.js`) are evaluated in 3‑D on the SDF chunk faces (feature size clamped to
   ≥ 2.5 voxels), so cliffs are as rugged as the mesh around them and the seam stays continuous.
   Rocks are seated on
   the final displaced surface. **True‑3D cliffs**: cells steeper than the cliff angle are grouped
   into chunks (16 cells, budgeted), the heightfield mesh skips their quads and masks its
   displacement around them, and a small pool of workers streams marching‑cubes meshes back in
   batches; the chunks use the same world‑space surface shader with `aux` sampled by XZ and hardness
   from the 3‑D strata model so beds read correctly on undercut faces. Only the band of voxels
   around the surface is sampled and polygonised; the fine roughness noise is band‑limited to ≥ 5
   voxels so it cannot alias into stair‑steps / "pancake" layers; the voxel size is either set
   The face is a *sheet*, so a wall has to be sampled from its foot to its crest — that, not the
   triangulation, is what costs: per chunk most of the time goes into the carving of each voxel
   (bedding, joints, blocks, relief). Two things keep it down: the sampled y‑band of a column is
   limited to where a carve can actually flip the sign, and everything that depends only on the
   plan position and the bed (joint lattice, block offsets, hashes) is computed once per bed and
   reused for every voxel of that bed. Chunks are built in a pool of workers (up to 8, one less
   than `hardwareConcurrency`) and streamed into the scene as they finish, so the terrain is
   interactive while the walls come in.
   explicitly or chosen automatically as the finest that fits a voxel budget (the HUD shows it).
6. **Rocks** (`src/rock-geometry.js`, `src/rock-placement.js`) — *off by default for now "
   `rocksEnabled` / `pebblesOn` are 0 while the cliff faces are being finished; switch them on
   in the **Rocks** card.* Eight archetypes per seed
   (boulder / block / slab / shard): displaced icosphere, anisotropic stretch, then clipped by
   bedding + joint‑set planes, crease‑aware normals. **Placement follows origins, not a scatter**:
   seven fields are derived from the terrain first — *face* (steep, hard, soil‑free), *crest*
   (relief above a regional minimum, so a butte top counts but a hummock does not), *apron* (a
   bucket‑sorted steepest‑descent sweep that carries the face's supply down the fall line, fining
   and thinning as it goes), *power* (√flow × tan slope = what a stream can still move), *brake*
   (flow decelerating: inside a bend, at a gorge mouth, where a valley opens up), *shore* (flatness
   in a ±3 m band of the water surface, from rivers, lakes *and* the sea) and *soil* (fine cover
   that buries rock). Each field is normalised against its own 95th percentile, then every
   candidate point picks its **strongest** signal and takes that category's own law: **bedrock in
   place** (boxy, bedding still horizontal, sunk deep, skipped on near‑vertical walls, which the
   true‑3D chunks already own), **talus** (largest blocks nearest the cliff, fining and thining
   downslope, tumbling more as they get smaller), **channel lag** (only what the simulated stream
   power can still move, wet‑darkened), **bars & fans** (rounded, flat, widest in plan, wherever
   flow decelerates), **caps, tors & pavement** (a residual cap crowns the high ground; on flat
   bare hard ground the same origin instead lays thin wide *slabs* packed together), **erratics**
   (their own sparse grid: apron fringes and lonely flats), **shore cords** (pebble to cobble,
   sorted across the band). A denser second pass lays up to 40 000 small rounded **stones**
   wherever that same story says gravel gathers — apron, bar, shore, channel, alluvium — never on
   roads or under water, so gravel is real geometry up close. Clasts are nudged along the joint
   lattice so the pieces of one bed line up in rows. Rendered as `InstancedMesh` with
   per‑instance tone variation and the same ACES tone mapping, water plane. The **Origins** card
   gives one weight per category plus *soil hides rock* and *size spread*, and the HUD line under
   *Rocks* reports how many of each were placed — the quickest way to tell a scree slope from a
   braid plain from a desert pavement.

**Sediment is its own layer, not a tint.** The fill that water and weathering lay down is *sorted*
by the stream power that carried it — gravel where the ground is both steep and well fed (4‑14 % of
a typical map), sand on the bars, silt and mud on the quiet flats, which is most of it — and it is
*laid in beds*: surfaces that thicken, thin and onlap the valley sides rather than being sawn through
them at one constant height, each bed with a dark parting on its base, its own tone and a set of
inclined cross‑laminae inside it. Its surface keeps *current ripples* — a wave train along a
palaeocurrent direction that wanders patch to patch, with analytic stoss/lee relief — and *mud
cracks*, desiccation polygons on the open silt flats. Where the fill is thick the rock beneath is
dropped entirely: no joints, no flakes, no micro‑relief, and the surface is matte and damp‑darkening
like real sediment. The bedding still reads *through* the vegetation as alternating pale and dark
strips, which is what an alluvial plain looks like from any height, and the coarse facies pushes the
grass back so bars stay bare. Every part of it is faded by the pixel footprint, so the ripples and
laminae dissolve at distance instead of crawling. Eleven sliders and three editable facies colours
under **Sediment → Sediment**: `deposit` decides where the fill lies, `flow` × slope decides what
grain it is, and *Bed thickness* is the strata of the *fill*, nothing to do with *Strata → Bed
thickness* of the rock underneath.

## Controls

The inspector is cut by *what a thing is*, not by when it runs — one outliner row per category, in
six sections:

| Section | Outliner rows |
| --- | --- |
| **Terrain** | Landform · Dunes & outcrops · Strata · Erosion |
| **Cliffs & mesh** | Cliff depth · Mesh & detail |
| **Water & access** | Rivers & lakes · Roads |
| **Props** | Rocks |
| **Surface** | Rock colour · Painted beds · Jointing · Grain · Mineral flakes · Spalling · Sediment · Soil & gravel · Plants & snow |
| **Environment** | Sun & atmosphere · Water · Viewport |

21 rows, 56 cards, 334 controls. Terrain rows rebuild the heightfield (press *Generate* or
Ctrl+Enter; the worker reports progress), *Cliffs & mesh* and *Roads* rebuild the mesh, *Rocks*
re‑scatters, everything under **Surface / Environment** is live. Cards that change stage inside a row
carry their own stage (the simulated‑river card is a terrain card inside a mesh row, for instance),
so splitting the categories did not change when anything recomputes.

* **Landform** — preset, elevation, relief, mesa & canyon, rugged outcrops (the push‑pull), rock
  stacks, grid (seed / resolution).
* **Dunes & outcrops** — transverse dune fields (height, wavelength, wind direction, asymmetry,
  coverage) and embedded core‑stone boulder clusters (density, size, count, spread, spacing, aspect,
  burial, weathering, prefer‑high‑ground, max slope). Outcrops are on in Granite tors, Granite domes,
  Boulder field, Icelandic highlands and Glacial valley.
* **Strata** — bed thickness and strength, the stratigraphic column (variation, packaging, hard
  share, lateral change, re‑cut) and geological dip.
* **Erosion** — fluvial incision (strength, iterations, concavity, uplift, deposition, fill, pits,
  cliff protection, rills, diffusion), hydraulic rain (droplets, speed, capacity, evaporation,
  inertia, radius, hardness influence) and thermal weathering (iterations, rate, talus angles).
* **Cliff depth** — the true‑3D SDF chunks (angle, undercut, bed contrast, pockets, joints, pits,
  roughness, push‑pull, blocks + block size/loss/swing, vertical wall, lean, blend, chunk size,
  voxels per cell and the voxel budget, max chunks) and face displacement (overhang, buttress,
  ledge noise).
* **Mesh & detail** — mesh subdivision (up to ×6, side capped at 4097), relief detail (amplitude,
  scale, cliff bias), rocky facets and crag.
* **Rivers & lakes** — the draw tools, the feature list, simulated drainage (catchment, width per
  √km², max width, floodplain, bank gullying and steps, depth scale, water fill, dry slope and dry
  big‑river cutoffs, braiding, lakes in depressions with fill / area cap / minimum area, guide flow)
  and the drawn guides (width, depth, bank angle, bank height, meander, water erosion). Drawn
  features persist with the settings and survive preset changes.
* **Roads** — width, shoulder, smoothing, cut and fill, shading, colour.
* **Rocks** — scatter (density, patchiness, patch size, follow slope), **Origins** (bedrock in place,
  talus, channel lag, bars & fans, caps & tors, erratics, shore cord, soil hides rock), rock shape
  (smallest / largest, angularity, embedment, size spread, seed) and the instanced gravel stones.
* **Surface** rows — **Rock colour** (palette → swatches, oxide, cavity, bump and base roughness),
  **Painted beds**, **Jointing** (strength, spacing, joint swing, block size varies, pattern warp,
  detail patchiness, groove width/depth, stagger, block tilt/tone, missing joints), **Grain**,
  **Mineral flakes** (three plate layers), **Spalling**, **Sediment** (cover, bed thickness, bed
  contrast, cross‑lamination, sorting, grain offset, ripples + wavelength, current direction, mud
  cracks, grain, slope limit, three facies colours), **Soil & gravel** (runoff staining, pebble
  gravel, soil) and **Plants & snow**.
* **Environment** — sun direction and atmosphere (turbidity, fog, exposure), water (meshes, sea
  level, colours, opacity, clarity, foam, shoreline wetness, dried‑bed shading) and viewport
  (wireframe, show rocks, auto‑rotate, isolate a single layer, exports).

38 presets: Alpine granite, Sandstone mesa, Canyon, Sea cliffs, Coastal cliffs, Quarry walls, Desert
stacks, Stack cliffs, Limestone escarpment, Fjord, Badlands, Dolomite towers, Desert buttes, Volcanic
island, Highland glens, Boulder field, Scree slopes, Granite domes, Granite tors, Rocky coast, Talus
canyon, Rocky mountains, Sand dunes, Icelandic highlands, Icelandic river plains, Himalayan peaks,
Alpine peaks, Karst pinnacles, **Braided river plain, Alluvial fan, Delta plain, Playa & mudflats,
Tepui, Sandstone forest, Glacial valley, Chalk cliffs, Basin & range, Tufa terraces**. The last ten
are the ones that exercise the newer layers: the four fill‑dominated worlds (braided plain, fan,
delta, playa) are mostly *Sediment* with channel lag and bars in the *Rocks* placer, Tepui and
Sandstone forest are wall worlds (true‑3D blocks, jointed faces, talus aprons), Glacial valley is the
erratic and moraine test, Chalk cliffs the shore‑cord test, Basin & range the tilted‑bed fault‑block
test and Tufa terraces the pale stepped‑pool test.

Picking a rock type fills the colour swatches; they can be edited freely afterwards.

**Drawing**: the toolbar over the viewport (or *Roads, rivers & lakes → Draw*) has Road / River /
Lake. Click points on the terrain; Enter or double‑click finishes a spline, Backspace removes the
last point, Esc cancels; a lake is one click. Drawn features are listed in the inspector with
delete buttons and per‑lake level sliders, persist with the settings, and survive preset changes.
**Water** (own outliner row) has the live sea‑level slider, colour, opacity, shoreline wetness and
the dried‑bed toggles.

**Viewport → Isolate layer** renders a single layer on its own (Strata / Grain / Flakes / Spalls /
Pebbles / Cover / Masks / Features) so each pattern and its scale can be judged before tuning it.

Exports (all maps share the heightmap's top‑down frame): OBJ / GLB (displaced terrain block +
all rock instances), 16‑bit packed heightmap PNG, **satmap** PNG (top‑down lit colour render at
2048², rocks and shadows included, same exposure/ACES chain as the viewport), splat masks PNG
(R rock · G scree · B wetness · A hardness), normal map PNG, viewport screenshot. Settings persist
in `localStorage`.

## Checks

```sh
node scripts/check-generate.mjs "Sandstone mesa" 256   # headless pipeline run + ASCII preview
node scripts/check-shader.mjs /path/to/glslangValidator # assembles the injected shader exactly as
                                                        # Three does and validates GLSL ES 3.00
```
