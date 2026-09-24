# The Space family — one container format, one file per part of a project

*Status: **shipped** — P1–P6 are implemented, gated and green (§12). Two claims are image comparisons and need a device;
they are reported as SKIPPED by the gate with their CPU half stated. The plan below is kept as written: where the code
diverged from it, the divergence is listed in §12 and explained in the source's own header.*

The ask, in the owner's words: stop loading `Projects/Project-Zero/Content/Scenes/*.gltf` at startup; give a project
one binary container of its own — like a font file, self-contained, with duplicate objects stored once as references;
usable either embedded or as a reference into engine content; opened from a command line the way Unreal opens a
project. And then the sharper half: the container should not be a single format but a **family** — a geometry file, a
UV file, a texture-paint file, a script, a terrain/environment file, each with its own extension, each of which can be
a file on its own *or* a part of a bigger file, and the project file contains them all or points at them.

The names chosen for that family are §2. The byte format is §3. What goes in which file is §4, how parts are embedded
and referenced is §5, how it opens is §6.

---

## 1. What exists today, and what is actually wrong with it

A level is a `.gltf` file under `Projects/Project-Zero/Content/Scenes/`. Some are committed (`CornellBox.gltf`,
`GlassProof.gltf`), the rest are **exported once on first run** from C++ that builds them analytically
(`RayTracingSolver`, `ShowroomStructure`, `MaterialStructure`, `ShaderBallStructure`) and then re-imported through
`SceneCodec` (`Engine/ContentInterchange/SceneCodec.cpp`) so every level takes the same path
(`GameExecution.cpp:76–98`).

That is a good *interchange* discipline and a poor *project* format:

| | today | consequence |
|---|---|---|
| container | glTF text + JSON | a full parse before the first pixel; ~4× the bytes of the records it means |
| scene graph | nodes/meshes/primitives | the resident records (`InstanceRecord`, `VertexRecord`) are rebuilt every run |
| duplicates | one node per copy | twelve copies of a sphere are twelve vertex buffers in the file |
| materials | glTF PBR + `extras.slate_*` | 58 floats × N re-derived through a codec on every load |
| UVs, paint, terrain | no carrier at all | they exist as code, not as data |
| session state | none | there is no editor context, no crash recovery, no save |
| fonts/audio/engine content | paths resolved ad hoc | `FontCodec::ScanEngineAndGameContent` is the only place the content-root convention is real |
| identity | file path | nothing in the file says "this is Project-Zero, revision 7, built by this exporter" |

The interchange codecs stay exactly as they are — they are how foreign art arrives. What is missing is a container
for **our** files, in our own records.

## 2. The family (the chosen names)

One structural rule makes the family work: **every file in it is the same byte format** (§3) and the extension says
what the file *is*. A `.geometry` is a container whose required table is `MESH`; a `.pigment` requires `PIGM`; a
`.projectspace` requires `META` + `SCEN`. The directory inside says what else it happens to carry. That is what makes
"individual file *or* embedded part" free: the same bytes are a file on disk, or a blob inside a bigger file.

| ext. | name | holds | writer | committed? |
|---|---|---|---|---|
| `.projectspace` | **Project Space** | the root: `META`, the level table (`SCEN`), project config, and a reference to every asset the project owns | editor / packager | yes — the thing you open |
| `.solution` | **Solution** | a bundle of projects: engine revision pin, content roots, toolchain, the list of `.projectspace` files | repo / CI | yes |
| `.geometry` | **Geometry** | vertices, indices, normals, tangents, bounds, LOD/cluster ranges | modelling / import | yes |
| `.uvspace` | **UV Space** | UV islands, seams, packing, texel density, the mapping a paint layer is authored against | UV editor | yes |
| `.material` | **Material** | **one self-contained material**: its parameters (the `MaterialRecord` + 288 B slab graph) *and* its textures — either embedded blobs or references to `.pigment`/image files. An object uses it by copying it in or referencing it (§4.2) | material editor | yes |
| `.pigment` | **Pigment** | a texture-paint document: layers, strokes, channels, resolution, brush refs; bakes into the `.material` that uses it | paint editor | yes |
| `.instance` | **Instance** | one placed object: transform + a `.geometry` + exactly one `.material`, copied or referenced (§4.2) | editor | yes |
| `.environment` | **Environment** | world staging: sun hour, fog scenario, atmosphere/moon/star settings, terrain heightfield and its material refs, the baked sky probe | terrain / sky editor | yes |
| `.script` | **Script** | code automation, one source or a small module | user | yes |
| `.workflow` | **Workflow** | an automation graph: import → bake → validate → package, its steps referencing `.script`s | user / engine | yes |
| `.archive` | **Archive** | a shipping pack: content-addressed blobs with the directory at the end (streamable) | packager | build artifact |
| `.runtime` | **Runtime** | the *resolved* launch configuration: level, tier, feature flags, backend, resolved content roots | the host, at launch | generated |
| `.state` | **State** | **one** file for the whole state family, told apart by a `domain` field inside `STAT`: `session` (where was I), `work` (unsaved buffers, crash recovery), `save` (a resumable world), `capture` (a read-only snapshot for comparison). **Deferred — see §11** | editor / game / harness | varies by domain |

Four notes on the naming, because three pairs were close enough to collide:

- **`.environment` is the world, not the config** (settled). The list offered `.environment` for dev/runtime
  configuration *and* `.enviromnt` for the terrain editor — one word, two jobs. In an engine "environment" is the
  world (sky, atmosphere, moons, terrain), and runtime configuration already has `.runtime`; so `.environment` is the
  terrain/sky file, which is also where the baked sky probe from the deferred environment-lighting plan lives. The
  config sense is `.runtime`, and nothing else.
- **The state family is one file type, `.state`.** It was four (`.context`, `.workstate`, `.state`, `.snapshot`) because
  four lifetimes were distinguishable — *where was I*, *unsaved work*, *resume the world*, *a read-only capture* —
  but a lifetime is a field, not a file type: the `STAT` table carries `{domain, revision, payload}` and the four
  domains ride that one extension. Fewer extensions, no lost meaning, and a single reader. **Skipped for now** —
  recorded in §11 so it is not re-litigated by accident when it lands.
- **`.runtime` stays its own file type** because it is not state: it is the *resolved* launch configuration the host
  writes at startup, and it is the one thing an editor, a game build and a crash report all want to read.
- **`.material` is a file, not a facet of the object.** The owner's correction, and it is the better design: a
  material is one self-contained file — parameters *and* textures — and an object **copies it in or references it**
  ("sphere 1 uses Clear Glass, sphere 2 uses Gold"). There is no per-object override list and no base/refinement
  split; §4.2 has the mechanism, and identical copies cost nothing in bytes because payloads are content-addressed.
  `.slate` stays avoided: the material domain's `slate_*` glTF extras already own that word.

Case: extensions are all-lowercase (`.uvspace`, not `.UVspace`). Git and Linux treat `.Geometry` and `.geometry` as
different files while macOS and Windows do not; a format family cannot afford that ambiguity.

## 3. The bytes (one format, TTF-like)

A font is a header, a directory of tagged tables, and payloads — and every reader, including ones that have never
heard of a tag, can walk it. Copy that, byte for byte where it helps:

    SpaceHeader                         // 16 B, little-endian
    ────────────────────────────────
    char     Signature[4];              // "FSPC"   — Frontier Space Container (the family's one signature)
    uint16_t Major, Minor;              // layout revision; a reader refuses a major it does not know
    uint16_t TableCount;
    uint32_t TableOffset;               // where the directory starts (header is 16 B, so usually 0x10)
    uint32_t TotalLength;               // self-report — a mismatch means a truncated download

    TableRecord                         // 16 B — the sfnt record, verbatim
    ────────────────────────────────
    char     Tag[4];                    // "MESH", "MATL", "PIGM", …  (the file's own TYPE is a tag too, see below)
    uint32_t Checksum;                  // FNV-1a 32 over the payload
    uint32_t Offset, Length;            // 4-byte aligned (4 is all std430 needs)

Rules that fall out of the layout, and that the gate checks:

- **One signature for the whole family** (`FSPC`), with the file's own *type* as a mandatory `TYPE` table holding a
  four-byte type tag + a schema revision. A reader walks any file in the family with the same code, then asks "do I
  know this type?" — which is exactly how `.geometry` and `.projectspace` share a reader.
- **Unknown tables are skipped, not refused.** A newer exporter may add `NAVI`; an older reader still loads the file.
- **A checksum mismatch is fatal for that table and named in the message.**
- **The payloads are the resident records, not a description of them.** `VertexRecord` 64 B, `InstanceRecord` 160 B,
  `MaterialRecord` 64 B + `MaterialSlabRecord` 288 B, `TriangleIndex` 64 B, `CameraRecord`, `PlacementRecord`,
  `PunctualLuminaireRecord` (all sized and static-asserted in `SceneStructure.h` / `MaterialIndex.h` /
  `SwapchainExchange.h`). Loading is a bounds check and a `memcpy` into the buffers `SceneStructure` already owns —
  no JSON, no per-run rebuild, a bit-identical resident scene by construction.

### 3.1 Tables

| tag | holds | lives in |
|---|---|---|
| `TYPE` | four-byte type tag + schema revision (what this file *is*) | every file |
| `META` | name, exporter + revision, build config, source list, pack/embed policy | every file |
| `MESH` | unique vertex/index buffers (`VertexRecord` + indices) | `.geometry` |
| `CLST` | cluster/LOD ranges (`ClusterRecord`) | `.geometry` |
| `UVSP` | UV islands: per-island vertex ranges, seams, texel density | `.uvspace` |
| `MATL` | `MaterialRecord` + 288 B slab graph — the material's parameters | `.material` |
| `PIGM` | paint layers, strokes, channels, resolution | `.pigment` |
| `INST` | `InstanceRecord` rows (transform + mesh/material slot) | `.instance`, `.projectspace` |
| `SCEN` | level table: names, instance ranges, node hierarchy (`PlacementRecord`), default camera | `.projectspace` |
| `CAMA` | `CameraRecord` rows | `.projectspace` |
| `LITE` | `PunctualLuminaireRecord` rows + the luminaire alias table | `.projectspace` |
| `ENVR` | world staging + terrain + the baked sky probe reference | `.environment` |
| `MSLT` | material slots: the `MaterialSlot` rows an `INST` table or a `.projectspace` names (§4.2) | `.instance`, `.projectspace` |
| `FLOW` | workflow steps (step type, operands, refs) | `.workflow` |
| `STAT` | the state family's one payload: `{domain, revision, payload}` with domain = session · work · save · capture | `.state` |
| `ARCH` | archive directory: hash → offset/length, chunked for streaming | `.archive` |
| `REFS` | the reference table (§5) | every container in the family |
| `TEXR` | texture index rows: URI/ref slot or blob index (a material's own maps) | `.geometry`, `.material`, `.pigment` |
| `BLOB` | raw embedded payloads, addressed by 64-bit content hash | any file that embeds |

### 3.2 One file per project, not per level

`Project-Zero.projectspace` holds every Project-Zero level as a `SCEN` row over one shared asset pool — which is also
where the dedup becomes visible: Showroom and Showcase draw the same chrome sphere, so there is one `MESH` payload,
one chrome material payload (named by both rows, referenced or copied — same bytes either way), and one `INST` row
per placed copy. Per-level files would duplicate exactly the content the owner asked to deduplicate; the directory
makes the multi-level case free.

## 4. The object model — "like an object"

A project is a tree of spaces, and the leaf edges are references:

    Project-Zero.projectspace                    ← what `-Project=` opens
    ├─ TYPE  projectspace · META  name, revisions, policies
    ├─ SCEN  levels: Showroom · Showcase · Materials · CornellBox
    ├─ ENVR  → Assets/Studio.environment          (sibling file, referenced)
    ├─ INST  rows … each row = transform + one geometry + one material (§4.1)
    │     ├─ Sphere_01   → Assets/Sphere.geometry (shared) · → Assets/ClearGlass.material  (REFERENCED)
    │     ├─ Sphere_02   → Assets/Sphere.geometry (shared) · → Assets/Gold.material        (REFERENCED)
    │     ├─ Sphere_03   → Assets/Sphere.geometry (shared) · → BLOB #7 = a full .material  (COPIED IN)
    │     │     └─ → Assets/Sphere.uvspace        (the geometry's UV space)
    │     └─ → EngineContent/…/panel.geometry     (engine content, referenced)
    └─ BLOB[] embedded payloads, keyed by content hash — a blob may itself be a whole space container

Two consequences worth stating plainly:

- **Geometry is shared; materials are the object's own.** Twelve identical spheres are twelve rows over one
  `.geometry` — but each row names its own material, either a shared `.material` file or a copy carried inside the
  object. "Sphere 1 uses Clear Glass, sphere 2 uses Gold" is the row, not a lookup table somewhere else.
- **Pieces know their neighbours by type, not by name.** A `.geometry` does not name its UV file; the pair is bound
  by a reference row from whichever file owns the pairing (the instance, or the project). Rename or move a file and
  nothing is stale except the reference path — which is why every reference also carries a content hash.

### 4.1 The instance row

    InstanceRow                         // 96 B
    ────────────────────────────────
    char     Name[32];                  // the outliner's label ("Swatch_17", "Sphere_02")
    float    Transform[16];             // object → world, column-major (the InstanceRecord's World)
    ReferenceRecord Geometry;           // 32 B — the .geometry this object instantiates (usually shared)
    MaterialSlot    Material;           // 24 B — the object's ONE material: copied in or referenced (§4.2)
    uint32_t LevelIndex;                // which SCEN level owns this row
    uint32_t Flags;                     // per-row bits (hidden, locked, cast-shadow, …)

### 4.2 The material — a self-contained file, copied or referenced per object

*"`.material` should rather be a single file that contains the texture/parameters of the material; the object just
copies the embedded material/file — sphere 1 uses Clear Glass, sphere 2 uses Gold."* That is the model, and it is
simpler than what this document had before (a base plus a per-object override list), so the override list is gone.

**A material is one file.** `.material` carries everything the material *is*:

    ClearGlass.material                  one file, one material
    ├─ TYPE  material · META  name, revisions
    ├─ MATL  the parameters: MaterialRecord + 288 B slab graph — the 58 floats, the lobes, the IOR, the coat
    ├─ TEXR  the texture slots that point at its maps (base colour, roughness, normal, …)
    ├─ BLOB[] the maps themselves — a `.pigment` bake, an imported image, an engine texture copied in
    └─ REFS  …or references instead of blobs: EngineContent/Textures/…, Assets/Checkers.pigment

So a material file is self-contained exactly the way a font file is: parameters and pixels in one place, no sidecar
required, and every part of it can be embedded or referenced independently (§5). A material with constants only (all
of M10 today) is just `MATL` and no blobs; a painted one carries its baked maps.

**The object names one material, and the slot says how it got it:**

    MaterialSlot                        // 24 B — inside InstanceRow.Material
    ────────────────────────────────
    uint8_t  Mode;                      // 0 Shared · 1 Copied · 2 CopyOnWrite
    uint8_t  Flags;                     // bit0 = shared-only (never fork this material) · bit1 = maps embedded
    uint16_t Reserved;
    uint64_t MaterialHash;              // FNV-1a 64 of the material's payload — identity, dedup, integrity
    uint32_t PathOffset;                // string table (Mode = Shared)
    uint32_t BlobIndex;                 // directory entry holding a full .material (Mode = Copied)

- **Shared** — the object references `Assets/ClearGlass.material`. Edit the file, every object pointing at it moves.
- **Copied** — the object carries its *own* material: `BlobIndex` points at a blob whose bytes are a complete
  `.material` container. This is exactly the "the object just copies the embedded material/file" behaviour: assign
  Gold to sphere 2 and sphere 2 holds its own Gold, frozen at the moment of assignment.
- **CopyOnWrite** — starts Shared; the first edit in the editor writes the copy in and flips the mode. This is the
  mode the editor defaults to, so a deliberate tweak never silently changes the other objects.

**Dedup is unaffected, because it keys on bytes.** Ten objects each holding their own copy of Clear Glass write *one*
blob and ten slot rows that name it: `MaterialHash` is the content address, so identical material payloads collapse
at every level — including the maps inside them. What is per-object is the *relationship* (each object owns its
material, may edit it, may not); what is shared is the *bytes*. The M10 census survives untouched: 49 grid cells,
49 material files (or copies), 49 distinct `MaterialDescriptor`s — which is exactly the uniqueness the level was
built to have, now stated in content terms rather than by a rule.

Copies nest for free: a copied material is a container inside a blob, and if its maps are embedded they are blobs
inside that blob. Reading a container is recursive, and the content hash of a copy is the hash of the bytes it was
copied from — so `Copied(ClearGlass.material)` has the same `MaterialHash` as the file it came from, and the two are
byte-identical by construction. That is what makes the last gate claim (§8, claim 8) checkable in one `cmp` — and a
copy that *is* edited simply re-hashes on save, so the slot always names the bytes that are actually there.

**Assignment defaults** (the one thing worth an explicit answer, §10 q4): the editor *assigns* a copy by default —
the owner's model above — while a project can mark a material **shared-only** (engine content, or a palette material
deliberately global), and any object can be switched back to Shared in the inspector. Nothing about the file format
depends on which default wins; the mode byte records what actually happened.

## 5. Embedded, referenced, or engine content

One record shape answers all three:

    ReferenceRecord                     // 32 B
    ────────────────────────────────
    char     Type[4];                   // which member of the family this reference names
    uint8_t  Mode;                      // 0 Embedded · 1 Sibling · 2 ProjectContent · 3 EngineContent · 4 ExternalSpace
    uint8_t  Flags;                     // bit0 = required (fail the load if unresolved) · bit1 = prefer embedded
    uint16_t Reserved;
    uint64_t ContentHash;               // FNV-1a 64 of the payload — dedup, integrity, and "same object twice" for free
    uint32_t PathOffset;                // into the string table (Mode ≥ 1)
    uint32_t BlobIndex;                 // into the directory (Mode = Embedded)

Resolution order at load: **Embedded → Sibling → ProjectContent → EngineContent → ExternalSpace → error naming the
exact path it looked for.** With `Flags.required` clear, a missing reference is a warning the editor surfaces; set,
it fails the load by name. The recommended default, and the one recorded in `META` so the artefact answers the
question by itself:

- **assets are embedded** when small (< 64 KB) or when the packager was told to;
- **assets are referenced** (sibling files) otherwise — that is the "individual file" half of the ask;
- **engine content is referenced** (fonts, audio archives, star catalogues, celestial textures) unless the packager
  was told `-Embed=Fonts` or the entry is marked required-embedded;
- **state is never embedded in an asset** — a hard line, so deleting a `.state` (any domain, including a crash
  `work` file) can never touch content;
- **a copy is a nested container**, not a special case: `MaterialSlot.Mode = Copied` points at a blob that is itself
  a complete `.material` (with its own directory, checksums and possibly its own blobs). Recursion terminates on
  the type tag, and a container that names itself as its own blob is refused by name.

Two lossless tools fall out of this, and both are provable:

- `-Pack`: inline every referenced sibling into the container, leaving hashes in place.
- `-Explode`: write every embedded blob out as its own file and leave a sibling reference behind.
- Round trip: `Pack(Explode(X)) == X` byte-for-byte, because every payload is content-addressed and the directory
  order is deterministic. (This is claim 1 of §8's gate, extended to the whole family.)

## 6. Opening it: the command line

The requested shape — `UnrealEditor -Project=Name -Build=Game -Config=Development +Location=(…)` — maps onto the two
hosts the engine already has (`EditorHost`, `GameExecution`), so the flags are a *parser layer*, not a new app:

    SlateEditor -Project=Project-Zero -Level=Showroom -Build=Editor -Config=Development
                +Location=(0,-6.0,2.6) +Rotation=(0,220,0)

    SlateGame   -Project=Project-Zero -Level=Showroom -Config=Development -game -silent

| flag | meaning here | lands on |
|---|---|---|
| `-Project=<Name>\|<path.projectspace>` | the project to open | `Projects/<Name>/<Name>.projectspace` |
| `-Level=<Name>` | select a `SCEN` level row | replaces the `--scene showroom` string aliases |
| `-Build=Editor\|Game` | which host runs | `EditorHost` vs `GameExecution` |
| `-Config=Debug\|Development\|Shipping` | switches + assert level | the existing `FRONTIER_DEBUG` build knobs |
| `+Location=(x,y,z)`, `+Rotation=(pitch,yaw,roll)` | transform override applied **after** the level loads | the level's default camera, so "play from here" is reproducible |
| `-Pack` / `-Explode` | family tools (§5) | the container writer |
| `-Bake=Sky` | produce the `.environment` sky probe | the deferred environment-lighting plan (§11.2) |
| `-Export[=All\|<Level>]` | (re)write the project from the code-built levels | replaces export-on-first-run in `GameExecution.cpp` |

`+`-prefixed trailing arguments stay last and take no `-` name, as written. The existing `--scene <file.gltf>` and
`--scale` stay as compatibility aliases for one phase so the CPU proofs in `Exhibits/` keep running unchanged.

## 7. Where the code goes (no new top-level folders)

- `Engine/ContentInterchange/SpaceCodec.{h,cpp}` — header + directory reader/writer, type registry, table
  (de)serialisation into `SceneStructure`/`TextureIndex`. Sits beside `SceneCodec`; `ContentCodec::Classify` gains
  `ContentFormatCategory::FrontierSpace` so every existing caller accepts a member of the family unchanged.
- `Engine/ContentInterchange/SpaceResolver.{h,cpp}` — `ReferenceRecord` resolution and the content-root scan (the
  same roots `FontCodec::ScanEngineAndGameContent` already walks).
- `Projects/Project-Zero/Source/CommandLine.{h,cpp}` — the `-Project/-Level/-Build/-Config/+Location` parser, shared
  by both hosts.
- `Tools/Scripts/PackProject.sh` / `ExplodeProject.sh` — the two lossless tools.
- `Exhibits/Workbench/ProjectFormat/` + `Exhibits/Gallery/ProjectFormat/` — the proof pair (§8).

## 8. What the CPU side can prove

Every claim is checkable on this machine, with no GPU:

1. **Round trip is lossless** — load, re-encode, `cmp`: byte-identical, directory order and checksums included.
   Extends to `Pack(Explode(X)) == X` for the whole family.
2. **The scene is the same scene** — the M10 material level loaded from a `.projectspace` renders bit-identical to
   the glTF path: `compare -metric AE` against `Exhibits/Gallery/Materials/MaterialLibrary_Wide.png`.
3. **Dedup is real, and copies do not duplicate bytes** — N duplicate spheres report 1 `MESH` entry, N `INST` rows and
   N material slots that all content-address the same `MATL` bytes; the file size is flat in N whether those slots are
   `Shared` (one referenced `.material`) or `Copied` (N private copies of one blob).
4. **References resolve, and fail loudly** — with `EngineContent/FontArchives/Inter` present the level loads; with
   the folder renamed the error names the path; with `-Embed=Fonts` the same level loads with the folder still
   renamed.
5. **The directory is forward-compatible** — a synthetic unknown table does not stop a load; a flipped bit in a
   payload is a named failure, not a corrupt render.
6. **The CLI is the same session** — `-Project=Project-Zero -Level=Materials +Location=(0,-5.0,2.6)` renders the same
   image as today's `--scene materials` invocation (AE = 0).
7. **A material file is self-contained and exact** — the M10 level carried as 49 `.material` files resolves 49
   distinct descriptors, byte-identical to the 49 the level builds today (census unchanged: Standard 17 · Aniso 2 ·
   ClearCoated 7 · Cloth 2 · Subsurface 7 · Transmissive 8 · EmissiveOnly 1 · Unlit 1); a constants-only material is
   `MATL` with zero blobs; a textured one resolves its maps from its own `BLOB`/`TEXR` with no sidecar present.
8. **Copied == referenced, byte for byte, and copying is free** — `Copied(ClearGlass.material)` compares equal (`cmp`)
   to the file it was copied from and carries the same `MaterialHash`; ten objects holding their own copies of one
   material produce one blob and ten slot rows (file size flat in N); editing a `Shared` material moves every
   referencing object, editing a `Copied`/`CoW` one moves exactly one (reference census differs in exactly one row),
   and a slot whose blob is missing fails by name rather than silently rendering the base colour.
One gate, `Exhibits/Workbench/ProjectFormat/CheckSpaceFamily.sh`: PASS/FAIL per claim, exits on the first red. Claims 1, 3,
4, 5, 7 and 8 run here in full (with the shell-level `Pack(Explode(X)) == X`); claims 2 and 6 have their CPU half
(the records ARE the resident records, by `memcmp`) run here and their GPU half (AE = 0 against the gallery PNG) reported
as SKIPPED, because this machine has no device — see §12.3.

## 9. Phasing

| phase | deliverable | gate |
|---|---|---|
| P1 | header + directory + `TYPE`/`META`; reader, writer, checksum; the gate itself | round trip, truncation, unknown table |
| P2 | asset files: `.geometry`, `.material`, `.instance` + `MaterialSlot` (shared / copied / copy-on-write) — enough for every existing level | bit-identical to the glTF path on the M10 level; claims 7–8 |
| P3 | `REFS` + `BLOB` + the five modes, `-Pack` / `-Explode` | dedup, engine-content resolution, embed fallback, pack/explode round trip |
| P4 | `-Project/-Level/-Build/-Config/+Location` in both hosts; `-Export`; migrate Project-Zero; glTF stays as interchange | CPU parity: package run == `--scene` run |
| P5 | `.runtime` only; the `.state` family (four domains in one file type) is **deferred** (§11) | `.runtime` records are written and read back; no state ever embeds into an asset |
| P6 | `.environment` (terrain + sky probe, per the deferred lighting plan), `.pigment` + `.uvspace` with the paint/UV editors, `.workflow`/`.script`, `.archive` | each gets its own exhibit pair when it lands |

Shipped as: P1 `SpaceFormat.h`/`SpaceCodec.{h,cpp}` · P2 `SpaceExport.{h,cpp}` · P3 the same codec's `SpacePack`/
`SpaceExplode`/`SpaceEmitWithReferences` · P4 `Projects/Project-Zero/Source/CommandLine.{h,cpp}` + `SpaceTool` +
`Tools/Scripts/{Pack,Explode}Project.sh` · P5/P6 `SpaceExport.cpp`'s six type exporters + `SpaceTool -Bake=Sky`.

## 10. Open questions — the ones worth answering before P2

1. **Layout revision.** The records are GPU-facing and have moved every milestone (R4a widened `MaterialRecord`).
   Store the revision in `TYPE`/`META` and refuse a mismatch, or write up-conversion steps? Plan assumes refuse, with
   the message naming both revisions.
2. **Compression.** v1 stores payloads raw so a container can be memory-mapped. If size becomes a problem, a
   per-table flag (`ZSTD`) copies WOFF's per-table compression — the directory already carries the lengths it needs,
   so no format change.
3. **One `.projectspace` per project, or one per level?** Plan assumes one per project (§3.2); a very large project
   might want to split levels into sibling `.projectspace` files joined by a `.solution`.
4. **Assignment default: copy or share?** The plan takes the owner's model — assigning a material *copies* it in,
   so every object owns its material — with `Shared` available per object and a `shared-only` flag for palettes and
   engine content. The trade-off is a decision, not a format detail: copy-by-default means editing a material no
   longer moves the twelve spheres that were given the same material (you would re-assign them); share-by-default
   means an "edit one object's material" needs the inspector to fork it first. Plan assumes **CopyOnWrite** as the
   editor default, which behaves like copy for edits and like share for bytes — and it is the one open question worth
   an explicit answer, because it is the only part of this design a user ever feels.

   **Answered: CopyOnWrite is the default.** It is what the code takes when it has to take one —
   `SpaceTool`'s export writes `kSpaceMaterialCopyOnWrite` slots that name the shared file and carry no blob, and
   `SpaceExport.h`'s note says so where the slot is created. The consequence is the one the trade-off predicts: an edit
   to a `CoW` material forks that object's copy (one blob appears, one slot row changes) and leaves the other referrers
   alone; a `Shared` material is the level designer's explicit "these are the same thing", and `Copied` is what a
   `CoW` slot becomes after its first edit. Claim 8 measures all three.
5. **Does a material file embed its maps or reference them?** Plan assumes both are allowed and the policy is §5's
   (embed small, reference large; `-Pack` inlines). A material whose maps are always embedded is a self-contained
   font-like artefact that survives being copied anywhere; one that references `EngineContent` is smaller and updates
   when the engine does.
6. **Terrain in `.environment` or its own file type?** Plan assumes `.environment` holds the heightfield reference
   plus the sky staging, with the heightfield itself a `MESH`-shaped blob — a separate `.terrain` file is easy later.
7. **Paint bake timing.** `.pigment` is the editable source and the `.material` carries the baked maps; the question
   is whether the bake runs on save (a material is always renderable) or at pack time (iterate fast, bake once).
   Plan assumes save, with the bake's input hash recorded so a stale bake is detectable; either way the GPU never
   reads a stroke list.
8. **Where does an *object's* material live when the object is a file?** Plan puts the `MaterialSlot` on the
   instance row (§4.1), so one `.geometry` can be instantiated twice with two different materials. A `.instance`
   file carries its own slot, so "the object's material" travels with the object; a bare `.geometry` has none.

## 11. Deferred, in writing (so it is not re-litigated by accident)

1. **The `.state` family** — one file type, four domains (`session` · `work` · `save` · `capture`), skipped by the
   owner's call. The container already carries it (`STAT`); what is deferred is the payload format for each domain,
   the autosave cadence, and which domains are ever embedded. Nothing else in the plan depends on it, which is why
   P5 ships `.runtime` alone.
2. **Environment lighting — bake the sky, and let the sky be a light.** The measured answer to *"does the ray tracer
   / ReSTIR sample the sun, sky, moon?"* lives in `Exhibits/Workbench/Materials/MaterialProofsReport.md` §13.12
   rather than here, together with the two-stage plan (bake a mipmapped probe per staging into the project's
   `.environment`; then add a sky candidate to the reservoir so sky direct lighting reuses). One number from it is
   worth repeating because it is what makes the deferral safe: an escaped ray currently costs **6.5–7.0 µs** on this
   CPU against ~12 µs for a whole path sample, so the cost is real but bounded — this is a performance and variance
   improvement, never a correctness gap.
3. **Editors.** `.uvspace`, `.pigment`, `.environment` terrain authoring and `.workflow`/`.script` authoring are
   format-defined here and editor-defined later; each gets its own exhibit pair when its editor lands (P6).
4. **Material *parameters* as a shared layer over a copied material** (deferred, and the idea the owner replaced).
   An earlier revision of this plan had a base `.material` plus a sparse per-object override list; the owner chose
   self-contained material files that objects copy instead. If a real need appears later — one gold with 20 slightly
   different roughnesses — the addition is a new *file type* (a variant that names its parent and lists overrides),
   never a quiet change to `MaterialSlot`; v1 has no such table and no such field.

---

## 12. What landed (P1–P6): files, gate, divergences, and what is still owed

### 12.1 The code

| what | where |
|---|---|
| the bytes: signature, 20 B header, 16 B table records, tags, the 13-entry file-type table, every row struct, FNV-1a | `Engine/ContentInterchange/SpaceFormat.h` |
| reader, writer, `-Pack`, `-Explode`, the shared re-emit, reference resolution | `Engine/ContentInterchange/SpaceCodec.{h,cpp}` |
| resident records → containers: geometry, material, instance, project builder, runtime, environment, uvspace, pigment, workflow, archive | `Engine/ContentInterchange/SpaceExport.{h,cpp}` |
| the launch line (`-Project`/`-Level`/`-Build`/`-Config`/`-Export`/`-Bake`/`-Embed`/`+Location`/`+Rotation`, and the `--scene`/`--scale` aliases for one phase) | `Projects/Project-Zero/Source/CommandLine.{h,cpp}` |
| the tool the CLI drives, and the two scripts the plan names | `Exhibits/Workbench/ProjectFormat/SpaceTool.cpp`, `Tools/Scripts/PackProject.sh`, `Tools/Scripts/ExplodeProject.sh` |
| the gate | `Exhibits/Workbench/ProjectFormat/{SpaceFamilyProof.cpp,CheckSpaceFamily.sh}` |

### 12.2 The gate, as it ran

`bash Exhibits/Workbench/ProjectFormat/CheckSpaceFamily.sh` → **19 claims passed, 0 failed, 2 skipped**, comprising:

* claims 1–8 of §8 (12 verdicts in the proof, 6 PASS / 2 SKIP of them the two GPU halves);
* the command line (P4): export the M10 level, `-Verify` every vertex/index/cluster and all 49 materials against the
  level builder by `memcmp`, `-Info` the container, `-Pack` it and resolve every reference with the content directory
  deleted, `-Explode` it back;
* the shell-level round trips: two exports byte-identical, `Pack(Explode(X)) == X` at 13 052 252 B, and the whole family
  present in one export (project, runtime, environment, uvspace, workflow, archive, geometry, materials);
* `-Bake=Sky`: a 32×16 equirect probe integrated from `AtmosphereModel::Integrate` at the staging's own sun hour.

Measured facts worth keeping: the M10 level exports as **49 references · 49 material slots · 49 instances · 49 material
files**, the project file is **14 116 B**, and the geometry behind it is **13 042 124 B** — i.e. the project is 0.1 % of
what it points at, which is the whole point of a reference. `-Verify` reports 49/49 record sets identical.

### 12.3 Still owed

* **AE = 0 on a device** for claims 2 and 6 (`Gallery` comparison between the project path and the `--scene` path). The
  CPU half is proven here and the gate says SKIPPED, never PASS.
* **Committing the exported artefacts.** `-Export` writes into `Build/Space` (gitignored) because it is 13 MB of
  regenerable bytes; `Tools/Scripts/PackProject.sh` is what a packaging run uses to write them where they ship.
* **The editors** (§11.3): `.uvspace`, `.pigment`, `.environment` authoring are format-defined and editor-defined later.

### 12.4 Where the code diverges from this plan, and why

Each of these is explained at the site in the source; they are listed together so the plan and the code cannot drift
silently.

1. **The header is 20 B, not 16** (§3's field list does not fit 16: `4+2+2+2+2+4+4`). The gate asserts the constant and
   `sizeof(SpaceHeader)` against each other, so this cannot rot.
2. **`INST` rows are 112 B and materials are a separate 24 B `MSLT` row**, not one combined 96 B record. The plan's own
   field list for that record is 160 B. Splitting is what lets two instances share one material slot and what makes
   claims 3 and 8 measurable.
3. **The directory is written after the payloads** (the header's `TableOffset` describes where), so adding a table does
   not move every payload, and `-Pack`/`-Explode` rewrites are shifts rather than re-layouts.
4. **`BLOB` is emitted last, sorted by hash, with offsets relative to its own payload**, and every blob's hash is
   re-checked on read — a flipped bit inside an embedded material is a named failure, not a corrupt render.
5. **`REFS` is re-emitted by the tools with its string block rebuilt** (rows, then strings). A tool cannot carry the
   offsets across a rewrite, and a payload whose first four bytes are text is a payload whose row count is text: that
   mistake was made once during this milestone and the writer's API now makes it unrepresentable
   (`WriteRowWithString`, and no "write a string now" call at all).
6. **`LITE` rows are `PunctualLuminaireRecord` in fixed form** (the CPU record carries a `std::string`) plus a 16 B alias
   row, which is what §3's table needed to be writable.
7. **`SpacePack` copies the container's existing blobs into the rewritten file first**, so a reference index that already
   pointed at one still does — and it CHECKS that the copy reproduced its index rather than assuming it.
8. **`-Explode` writes the rewritten container into the payload directory**, not beside the input: its references are
   sibling names, and a sibling is "next to the file that names it".
