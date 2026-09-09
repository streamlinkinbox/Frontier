# Session conventions — naming, patterns, preferences, links

Distilled from the full R2→R4b conversation (2026-09-04). Purpose: a new session can pick up the
vocabulary and working rules without re-reading the whole thread. Authoritative rules still live in
`CLAUDE.md` and `AgenticInstructions/SKILL-Naming-Formatting.md` (skill file wins on conflict).

## 1. Naming (decided, with rejections)

* Interchange module: **`ContentInterchange`** — the word *Asset* is disliked; never use
  `AssetInterchange` (the previous project/Slate name) or any new `Asset*` name.
* Material names **start with Material** (user): **`MaterialDescriptor`** (one material: `Slabs[]` +
  `Operations[]`, OpenPBR identifiers), **`MaterialIndex`** (resident table addressed by material id;
  owns the GPU record layout — CLAUDE.md role 15, direct addressing), **`MaterialCodec`**
  (glTF/MaterialX ↔ descriptor, role 2). Per-slab GPU record **`MaterialSlabRecord`**; 64 B header
  **`MaterialRecord`** (R4a: 288 B slabs; plan said 256 — flagged deviation).
* Rejected material names: `MaterialStructure`, `SurfaceSpecification`, `SurfaceStructure`
  (user disliked all three). Do not propose `Surface*` / `*Binding` names again.
* Scene-graph rows: **`PlacementRecord { Name, Ancestor, FirstDescendant, NextPeer, Local, World,
  instance range, camera, luminaire }`** — because `Node`, `Parent`, `Child`, `Sibling`, `Hierarchy`
  are all banned words. `Substrate` is banned too: never use Unreal's term in code.
* Other established names: `TraversalIndex` (role 15), `ShadingTableCodec`, `TextureIndex`,
  `SceneCodec` / `FbxCodec` / `ObjCodec` / `ContentCodec` (dispatcher, by extension),
  `ShaderBallStructure`, `DiagnosticInspector` (user's UI vocabulary word is *Inspector*),
  `RenderScheduler`, `ReSTIRIntegrator`, `SwapchainExchange` / `VisibilityExchange`.
* Scene-graph records are **data only, no UI** — the future outliner reads `PlacementRecord` directly;
  GPU culling path stays unchanged.

## 2. Working rules (standing)

* **Plan first, code only after approval** — one short phase plan in `References/` per phase, then rows.
  R4 was split into **R4a (data)** + **R4b (shading)** as a deliberate deviation: the data contract must
  leave Cornell/Sponza pixel-identical (proves records are read correctly), while EON/GGX/coat/fuzz/film
  change every pixel by design — one commit for both would hide record bugs inside BRDF changes.
* **One commit per plan row**, pushed to the session branch (`arena/*-slate`; this session is fixed to
  `arena/01a06c54-slate` — never switch/create/push other branches). Commit messages carry the row scope.
* Every row ends with: **proofs** (CPU harness logs committed in `Scratchpad/`), **hardware acceptance**
  list for the user's GTX 1060, and **deviations flagged with ⚠️** (plan said X, did Y, why).
* Phase order (v2.2 as amended): R2 resident scene → R3 CWBVH (straight after R2, no R2b) → R4a → R4b →
  R5 hardware AS (`rayQuery`), R6 ReSTIR DI, R7 GI + denoiser, R8 H-PLOC/LOD/splatting.
  USD/MaterialX codecs (`tinyusdz`, `MaterialXCodec`) are future; `UsdCodec` after FBX/OBJ.
* Sandbox limits (do not re-discover): **no Vulkan runtime, no glslang** here — shaders are eyeball-only
  plus the **1:1 port discipline** (the `.slang` text itself compiled as C++ via `Scratchpad/GlslShim.h`,
  `.xyz→.xyz()` rewrite). `/home/user/fr` overlay can be wiped by sandbox resets (rebuild from
  `SultanAladin/Slate` arena branches); resets can also drop commits from history (re-commit identical tree).
  GitHub auth is preconfigured — on failure tell the user to reconnect in Arena, never ask for tokens.

## 3. Conventions / patterns

* Proofs: white-furnace + reciprocity + sampler-vs-integral harnesses; Cornell **pixel-identity baseline**
  (R0→R3 reference; R4b pins `specular_weight = 0`, re-export 9067 B `93c67d50…`, geometry byte-identical);
  Sponza decode table (tris / instances / materials / textures / MB / ms); KHR round trips; cross-tool
  FBX/OBJ axis checks (Blender Z-up/m, Maya Y-up/cm, Max Z-up/in).
* Test level: **Crytek Sponza** (Khronos glTF-Sample-Assets, ~262 k tris, CC-BY) **fetched at build time
  (`FetchSponza.ps1`), never committed**. Test scenes git-ignored (`Sponza/`, `ShaderBall.gltf`).
* Debug views = **small popup overlay** (user), not a settings page: **F3** opens/cycles, Shift+F3 back,
  **F4** toggles HiZ, Esc closes; Notch-card styling via `ControlKit::Palette()`; persists as
  `[render] debug_view`. R4b views: Roughness 8 / Metalness 9 / ShadingNormal 10 (vertex normal only).
* Config keys under `[render]`: `slab_limit` (Tier A 1 flattened, Tier B/C 4, ceiling 8),
  `texture_edge_limit`, `debug_view`, `occlusion_culling`, `ray_tracing_tier` (`auto|software|ray_query`;
  a tier is never faked — downgrade + toast). `.manifest` banned → `.toml` via tomlpp.
* Engine rules: shared code in `Engine/` (projects only wire scenes/tuning); shaders shared via
  `#include` from `Engine/Shaders/` (GLSL-in-`.slang`, lowered by slangc); scratch work **only** in
  `Scratchpad/`; PS 5.1-safe scripts; file headers 142 wide / section banners 122 / 4-spaces / Allman /
  flat `namespace Frontier` / `[unit]` annotations / unicode math glyphs.
* Coordinates: **RH +Z up, metres**; ufbx asked for RH Z-up metres directly; OBJ assumes Y-up + `--scale`.
  Colour: **linear Rec.709 internally, ACEScg only at codec boundaries** (`lin_rec709` declared).
* Tier A (compute, GTX-class) is the **primary** path, not a fallback; Tier B `rayQueryEXT` behind one
  `TraceClosest/TraceAny` function (specialisation constant); Tier C placeholder.
* Bindings (compute set 0): 2 materials, 10 slabs, 11 vertices, 12 indices, 13 energy LUT, 14 sheen LUT,
  **15 bindless `Textures[]` last** (variable-count binding must be last; moved from 11 in R4b).
  Raster set gains 5/6/7 only with descriptor indexing, else mask off. Push-constant ex-`TriangleCount`
  slot = `AlphaMaskedMaterialCount`. Alpha rule: `geometry_opacity × baseColor.a < AlphaCutoff`.
* Sandbox rhythm: C++ TUs `-fsyntax-only`/`-Wall -Wextra` against the overlay; `chk.sh` when present;
  build lists in both `CMakeLists.txt` and `ToolchainSequence.ps1`; submodules pinned (12, incl. ufbx
  v0.23.0, fast_obj, stb, cgltf, tinybvh @0e45842); PSAs (screenshots, kernel ms, Sponza counts) belong to hardware.

## 4. Material doctrine (from `MaterialSystemResearch-2026.md` — the long-term bets)

* Authoring record = **OpenPBR Surface v1.1.1 verbatim** (49 params, 9 groups), **not** an Unreal-flavoured
  invention. Runtime architecture copied from **Substrate**: slab = interface + medium, composed with
  `VerticalLayer` / `HorizontalMix(mask)` / `Weight` / `Coverage` operators, lit through complexity
  classes so a wall never pays for the car.
* Beyond-OpenPBR: **`slate_haziness_*`** (second roughness, Barla 2018) + **`slate_glint_*`**
  (Deliot–Belcour 2023) included from day one under `slate_` (glTF `extras.slate_*`); specular-profile
  LUT deferred (no measured data). Lobe set: EON (JCGT 2025, CLTC) → GGX + Kulla–Conty split-sum LUT +
  spherical-cap VNDF → F82-tint metals → Belcour–Barla thin film → haziness → coat (TIR inversion,
  roughening, darkening Δ, view absorption) → Zeltner LTC fuzz → emission → alpha-mask.
* Slab count **never baked into the format**: record = unbounded `Slabs[]` + `Operations[]`; runtime cap
  is config (`slab_limit`), importer folds + reports. **Texture painting = horizontal layers** (bake into
  the touched slab — free at runtime); only structure-changing layers become vertical slabs. **Car paint =
  body (F82) → flake (glints) → coat (+ thin-film pearl)**, room for dust/rain under 4; Tier A flattens
  with glints kept as a lobe modifier (degrades, never vanishes). Drivers: user plans layered texture
  painting + complex car paint — flexibility without stiffness is the requirement.
* Interchange: glTF ratified `KHR_materials_*` map 1:1 to OpenPBR (+ alignment drafts
  fuzz/coat/diffuse_roughness/subsurface appearing in loaders); FBX/OBJ collapse to Simple class.

## 5. Key links

* OpenPBR v1.1.1: `https://academysoftwarefoundation.github.io/OpenPBR/`
* Substrate overview (UE 5.3→5.8): `dev.epicgames.com/documentation/…/overview-of-substrate-materials-in-unreal-engine`
* Substrate internals (Epic Games Japan, GCC2026): `docswell.com/s/EpicGamesJapan/K8N2G7`
* EON (Portsmouth–Kutz–Hill, JCGT 2025): `https://arxiv.org/html/2410.18026v3`
* LTC sheen table (Zeltner et al.): `https://github.com/tizian/ltc-sheen` (Apache-2.0)
* glTF extensions registry: `github.com/KhronosGroup/glTF/tree/main/extensions`
* Sponza source: Khronos `glTF-Sample-Assets` (Crytek Sponza, CC-BY)
* Papers: Dupuy–Benyoub spherical-cap VNDF (HPG 2023); Deliot–Belcour glints (HPG 2023 / CGF 42(8));
  Zeltner–Burley–Chiang LTC sheen (SIGGRAPH Talks 2022); d'Eon–Weidlich VMF diffuse (CGF 43, 2024);
  Cocco anisotropic IBL (EG 2024)
* Code: ufbx v0.23.0 / fast_obj / stb submodules; tinybvh @0e45842 (MIT); overlay base
  `SultanAladin/Slate` arena branches
* UI + C++ reference (older incomplete project): `SultanAladin/Slate@arena/01a062a4-slate` →
  `References/UIComponents.html` ("Slate UI — Base Component Kit": design tokens + component kit;
  local copy already in tree, 975 lines — use for UI styling and C++ patterns)
* In-repo: `References/MaterialSystemResearch-2026.md` (§7 decisions, §7.1 slab design, §8 sources),
  `References/RestirRealtimeArchitecturePlan-v2.md` (tiers, frame graph, R4–R8 table),
  `References/RestirPhaseR{2,3,4a,4b}*.md` (phase notes + plans)

## 6. Git safety (Arena sandbox — incident 2026-09-04, do not regress)
* The sandbox once rewound `arena/01a06c54-slate` to its base commit: local commits vanished from `git log`
  (the remote still had them). A blind `git add -A` + commit then squashed pushed history into one commit
  and swept up session files. Recovery was: fetch the remote ref, `git reset --mixed origin/<branch>`
  (keeps the working tree), stage explicit paths, recommit — no force-push, nothing lost.
* Rules, every time: (1) `git log --oneline -4` must show the expected tip before committing; (2) stage
  **explicit file paths, never `git add -A`**; (3) one edit per file per agent message (the edit tool
  silently drops all but one same-file edit per batch — verify with per-item `grep -c` before committing).
* `Tools/git-hooks/pre-commit` enforces rule (1) automatically: it fetches the remote tip and blocks the
  commit unless the tip is an ancestor of HEAD (offline = warn-and-allow). Reinstall after a fresh clone:
  `cp Tools/git-hooks/pre-commit .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit`.
