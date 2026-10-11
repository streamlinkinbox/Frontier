# Base Mesh & Fracture Editor Port — Working Notes

Dated notes for the native port of the Project Zero editor's base meshes and of the separate fracture
editor page. The delivered description lives in `Docs/InspectorDepotConversion.md`; this file records
what was tried, what was rejected, and what was measured.

## 2026-10-08

### Branch

Continued from `darkenigmainbox/Frontier` `arena/6d5afbaa-frontier` rather than from scratch. Merged with
`--allow-unrelated-histories`; 125 add/add conflicts, all resolved in their favour, since their root
commit is a strict subset of ours and their files are the newer port. Merge `81cf192`.

### Base meshes — what was wrong before the port

The native editor had five base meshes it could construct and no way to tell them apart:

- `EngineContent/Icons/` ships `editor-{cube,sphere,cylinder,torus,cone}.svg`, but `IconSymbols.inc`
  registered 151 of the 161 SVGs and those five were among the ten missing. Every geometry row fell back
  to `IconSymbol::EditorMesh`.
- With no icon to read, `InspectorPanel.cpp:317` recovered the fracture card's primitive by matching
  `Picked.Label` against the five names. The browser never does this — `FractureSpecification.js` reads
  `Subject.Icon` — and the label is wrong in both directions: a duplicated "Cube 2" fails the match, and
  an imported mesh named "Cone bracket" passes one it should fail.

**Rejected:** widening `EditorGlyph`. It mirrors `OutlinerIconCategory` 1:1 offset by `Auto`, so adding
five entries means touching every consumer of both enumerations. The identity rides on
`EditorInstance::Artwork` (an `IconSymbol`) instead, which already existed and already won over the
category in `OutlinerPanel::ArtworkFor`.

**Checked before editing `IconSymbols.inc`:** both baked manifests
(`EngineContent/Icons/Baked/{Manifest,NativeConstructManifest}.json`) are name-keyed, not index-keyed, so
inserting the five enumerators in alphabetical position does not shift any baked lookup.

**Trap hit once, recorded so it is not hit again:** `ConstructKind` is
Cube/Sphere/Cylinder/Cone/Plane/Torus; the roster is Cube/Sphere/Cylinder/Torus/Cone. The first version
of `EditorFeedSequence.cpp` indexed `kBaseMeshArtwork` by `PlacementRecord::BaseMesh` directly and gave a
torus a cone's icon. Both crossings now go through a named function — `ConstructArtwork` and
`MarkerPrimitive` — and the proof asserts `int(Primitive::Cone) != int(Solid::Cone)` so a future cast
fails loudly.

`SceneCodec` was deliberately **not** touched: `PlacementRecord::BaseMesh` is authoring-only, defaulted
to `kNoBaseMesh`, and nothing serialises it yet.

### Fracture editor — what the bundle actually ships

Read from the built `Experimental/FractureEditor/index.html`, not from the `.js`/`.css` beside it.

- The embedded ProjectZeroEditor rules come **before** FracturePanel.css in the one `<style>`, so
  `input[type="range"]` keeps its gradient, height 26, radius 30 and border 0 even though the later
  `input,select{…border-radius:5px}` would otherwise flatten it. `select` does end at height 32,
  radius 5, padding 9. This ordering was verified in the 167,702-byte style block, not assumed.
- `MATERIALS` is not inlined in the page. The Gc and density figures come from
  `SourceDepot/Fragmentation/src/fracture/materials.ts`, and `BundleParity.py` now checks them there.
- The bake dot's three colours (`#89a591`, `#aa795a`, `#555`) are assigned by `Refresh()`, not by the
  stylesheet, so they are checked against the script text rather than the sheet.

### Deliberate deviations

| Deviation | Why |
| --- | --- |
| The `#viewport` canvas is stood in for by `CheckerViewport`'s analytical marker | UI-only port; no solver and no renderer behind it. The toolbar, overlay, controls and metrics are as shipped. |
| `SdfBlockHeight()` deleted | It was dead and wrong — it called `ProseHeight(nullptr, …)`. `StoredCardHeight` measures the SDF block inline. |
| `BaseMesh` kept as a spelling | `Base` and `Mesh` are both on the `SKILL-Naming.md` banned list, but "base mesh" is the reference's own term for these five analytical primitives and the register already uses it. Flagged for `Plans/Ongoing/BannedWordRemediation.md` rather than renamed unilaterally. |
| `Paint…`, `Settings`, `Mode` kept | Established vocabulary of the sibling `Engine/Editor/*Surface.h` headers. Diverging in two files would be worse than the existing debt. |

Renamed to clear the skill where nothing in the reference anchored the word: `Model` → `Subject`,
`Kind` → `Solid` (the roster) and `Shape` (the field), `Bake` (the enumeration) → `Freshness`.
`Storage` could not be used for the last of these because `Subject::Storage` is already the "Browser ·
per object" row.

### Measurements

| Check | Result |
| --- | --- |
| `Exhibits/Workbench/BaseMesh/RunNativeBaseMesh.py` | PASS 66, four captures |
| `Exhibits/Workbench/FractureEditor/RunNativeFractureEditor.py` | PASS 121, six captures |
| `Exhibits/Workbench/BundleParity.py` | PASS 428, up from 235 |

The fracture-editor page is captured at 1440 × 900 (dynamic, baked + SDF, fragments, disabled),
1120 × 760 (the ≤ 1180 breakpoint) and 1280 × 2080 (one tall frame so the whole inspector column is on
the page rather than under its scroll).

---

## 2026-10-08 — outliner metadata and the folder inspector

### What was tried and rejected

- **Converting the celestial rows to the browser's wording.** The feed already prints air mass for the
  sun, a solved limiting magnitude for the stars and a visibility distance for the fogs. Those are real
  engine readings and strictly richer than the browser's `{Intensity}× · {Angular diameter}°`. Rejected:
  the ask was the *missing* metadata, and overwriting a computed figure with a slider echo loses
  information. Only the three placeholders — `Live`, `Study`, `+0.0 EV` — were replaced.
- **`CompactNumber` exactness against `Intl` for float-sourced readings.** `-12.45f` is `-12.4499998`
  at float precision, so it legitimately prints `-12.4` where JavaScript's double prints `-12.5`. The
  relative `1e-12` nudge closes the double case and nothing wider; a nudge big enough to fix the float
  case (~1e-7) would round genuine readings wrongly. Accepted and noted in the header.
- **A five-part in-place patch script that writes once at the end.** A late needle missed and discarded
  four good edits with it. Rewritten to verify every needle before writing.

### Measurements

| Check | Result |
| --- | --- |
| `Exhibits/Workbench/OutlinerMetadata/RunNativeOutlinerMetadata.py` | PASS 51, one capture |
| `Exhibits/Workbench/Folder/RunNativePanelProof.py` | PASS, seven captures |
| `Exhibits/Workbench/BundleParity.py` | PASS 498, up from 428 |

### Notes

- `Exhibits/Workbench/Folder/` had no runner at all: `CheckEditorProof.sh` points at
  `Exhibits/Workbench/FrontierMirror/RunSolidArcMirror.py`, which is not in this tree. The new
  `RunNativePanelProof.py` links the shipped inspector family and `CelestialSolver.cpp` directly, so
  the capture is of the real `InspectorPanel::RecordCollection`, not a mirror of it.
- `RecordCollectionProof` is behind `FRONTIER_DEVELOPMENT`; the runner defines it. It gained an
  optional search string so the no-match state can be captured.
- The folder fixture had two rows that contradicted the reference scene: `Lighting` was a light with an
  atmosphere glyph, and `Moons` was geometry. Both are folders now, and `Moons` is hidden so its child
  proves the `Hidden by ancestor` wording. An empty `Staging` folder was added for the empty state.

---

## 2026-10-08 — merging Project-Networking, and the replication port

### The merge

`c7egoist/Frontier` branch `arena/9928a45d-frontier` is a lean 83-file tree that nests everything
under `Frontier/` and keeps proofs in `VisualProof/`. Merged with `--allow-unrelated-histories`; the
only conflicts were `README.md` (kept ours) and `.gitignore` (kept ours, plus their `Build/Output`,
`ThirdParty`, `*.local.*` and `.env` rules). Their two trees were then relocated onto this layout:

| Theirs | Here |
| --- | --- |
| `Frontier/Projects/Project-Networking` | `Projects/Project-Networking` |
| `VisualProof/Networking` | `Exhibits/Workbench/Networking` |

`RunChecks.py`, both `ToolchainSequence` scripts and `networking-windows.yml` all carried the old
paths and were corrected with the move.

### What was tried and rejected

- **Replicating through `TransportRouter`.** The router picks between EOS and Photon for one process
  and owns the premium decision; replication needs a carrier it can send on and be handed arrivals
  from, which is a smaller thing. A separate `ReplicationLink` keeps the router's premium rule out of
  the snapshot path, and makes the loopback carrier — the one the proof runs on — possible at all.
- **Letting a carrier own the replicated readings.** Rejected: the simulation already keeps them.
  A description declares offsets into bytes the caller owns, so there is one copy, not two.
- **Stepping over an unknown placement inside a snapshot.** Without its description there is no way
  to know how many bytes its properties take, so the identity after it cannot be found. The snapshot
  stops at the first unknown placement and is counted, rather than guessing.
- **`-Wformat-truncation` on the Photon reading.** Photon's reading is 256 bytes and the link's is
  192. Clipped with `%.*s` deliberately — the cause code sits at the head of the string, which is the
  part a reader needs.

### Measurements

| Check | Result |
| --- | --- |
| `Exhibits/Workbench/Networking/RunReplicationChecks.py` | PASS 66, and PASS again under ASan + UBSan |
| `Exhibits/Workbench/BundleParity.py` | PASS 498, unchanged by the merge |
| `Exhibits/Workbench/{BaseMesh,Fracture,FractureEditor}` runners | PASS 66 / 91 / 121, unchanged |

### Notes

- `PhotonTransport` counted arrivals and dropped them. It now carries `AttendPhotonPackets` and
  `InspectPhotonRoom`, so the replication sequence can hear a packet and read its own room slot,
  occupancy and master-client standing. `PhotonLinkStub.cpp` answers both honestly.
- Project-Networking is not in the root `CMakeLists.txt` and should not be: it needs the EOS SDK,
  which CI fetches under hash verification. The replication half needs neither EOS nor Photon, so it
  builds and proves itself on a plain runner.
