# Frontier · Mantis anatomy study

An original, three-dimensional **Mantis religiosa** (adult female) with a deformation skeleton, PBR materials, and four embedded animation clips. This is the undecorated base animal: **no orchid petals, fictional attacks, vehicles, or flower camouflage** have been added.

## Open the model

```bash
npm ci
npm run dev
```

Open the Vite address. The viewer supports orbit, pan, zoom, camera presets, wireframe, skeleton, anatomical annotations, animation scrubbing, playback speed, and a reference notebook. Keys **1 / 2 / 3 / 4** select Idle / Walk / Attack / Stance. **Space** pauses. **Esc** exits focus mode.

**Portable asset:** [`public/models/mantis.glb`](public/models/mantis.glb). The Download model button downloads this same file. Import it into Blender, Godot, Unity with a glTF importer, Unreal with its glTF/Interchange importer, or another glTF-compatible application. No browser-side animation code is required to play the embedded clips.

## Interactive Target test

The viewer now opens in **Target test**. Switch to **Animation clips** to inspect the four original clips.

1. **Drag the amber bead** in the viewport. Camera orbit is disabled only while the bead is being dragged. Use **Direction**, **Distance**, and **Height** for precise placement/depth; target positions are constrained to a forward arc.
2. Press **Lock & strike** (**F**) or enable **Strike when locked**. The mantis tracks the bead with its head, acquires a stable lock, leans in slowly, then makes a fast target-aware sweep/clamp.
3. A successful catch holds the bead between the two forelegs and draws it towards the mouth. **Release target** opens the grippers before the bead drops to the floor. **Reset** (**R**) restores its last placed position.
4. Targets outside the rig’s grasp envelope are flagged and cannot trigger a strike. Moving the target during preparation interrupts and re-acquires it. Moving it after commitment can produce a **miss**; it is not remotely attached just because an attack finished.
5. Playback speed also controls the simulation. **Space** / the pause button freezes the behavior; **Approach timing** adjusts the authored slow lean. Aiming guides can be hidden.

The 4.6 mm test bead is an interaction/controller aid, not a second creature. The shaded ground sector indicates the **allowed placement arc**, not a promise that every height/distance inside it is reachable.

### What is and is not exported

**The target controller is runtime Three.js code, not an extra animation inside the GLB.** `public/models/mantis.glb` and its four accepted clips are unchanged by this update. No target, guide lines or UI are baked into the animal asset. The simulator poses the same bones with analytical IK and uses contact proxies on both femur/tibia pairs. It is not a general rigid-body physics engine, a model of actual mantis vision, or measured species-specific behavior. Lock/lean durations are tunable presentation choices.

- `src/hunt-rig.js` — data-driven bone measurements, foreleg hinge-plane aiming, two-link grip-circle IK, planted support legs, head aiming and contact queries.
- `src/hunt-simulation.js` — deterministic 240 Hz state machine: track → lock → lean → committed strike → capture/retract/hold, or miss/recover; release/reset and auto mode.
- `src/hunt-view.js` — draggable 3D bead, ray/plane interaction, guide geometry and UI bindings.
- `src/hunt-config.js` — placement bounds, target radius, timings and contact tolerances.

`npm test` checks physical reach, hinge planes, no limb scaling, foot anchoring, no capture teleport, late-movement misses and fixed-step contact handling. With the dev server running, `npm run test:hunt` checks the actual drag, sliders, controls and mobile layout in a browser.

## Asset contents

- Real mesh geometry, not a rendered image on a plane.
- 43 deform bones, one shared skeleton, rigid-weighted chitin sections and blended antennae/abdomen.
- Triangular head capsule, paired compound eyes, three ocelli, mouthparts and two multi-jointed antennae.
- Elongated pronotum; paired coxae, spined femora, opposing tibiae, terminal hooks and five-part fore-tarsi.
- Four supporting legs with five-part tarsi, paired claws and fine setae.
- Segmented abdomen, lateral spiracles, terminal cerci, separate pairs of forewings and folded hindwings. The pale hindwing fans have an embedded `DisplayFan` morph target for the defensive stance.
- Embedded base-colour, normal and roughness maps, including reticulate wing venation and compound-eye microstructure.
- Coordinates authored in centimetres, with an export root conversion to **metres**. Approximate anatomical body length: 70 mm, excluding antennae and outstretched legs.
- This is a high-detail study mesh, **not a production crowd/LOD asset**. See `public/models/asset-manifest.json` for measured export statistics.

### Animation clips

| Clip | Duration | Intended use |
| --- | --- | --- |
| `Idle` | 6.0 s | Seamless sensing loop: independent head/antenna motion and slight abdominal ventilation. |
| `Walk` | 2.4 s | Slow, in-place wave gait: left hind → left middle → right hind → right middle. At least three supporting feet remain planted. Nominal forward speed **0.0025 m/s** at natural size. |
| `Attack` | 2.4 s | Folded setup, coxal approach, rapid overlapping femoral sweep / tibial clamp, retraction to the mouth, hold and recovery. One-shot playback by default. |

| `Stance` | 6.0 s | Seamless held threat display: upright prothorax, raised/spread forelegs, visible inner-leg markings and lifted wings with membrane fans. Blend into this loop over ~0.65 s. |

Idle and Stance are sampled at **60 Hz**, Walk at **120 Hz**; the revised Attack is sampled at **240 Hz** to retain the short sweep/closure window. Constant/redundant tracks are then optimised. The walk is intentionally in-place: supply forward translation from your character controller at the speed above. The viewer labels this as a **tracking view** and scrolls its reference grid at that same speed. Unlike the old loop, relative planted-foot speed is exactly the opposite of controller speed, and the swing trajectory matches it at lift-off and touchdown. The scrolling grid is a viewer aid, **not baked root motion**. There is no gameplay collision, damage, navigation or vehicle interaction yet. The attack has a 30 ms authored femoral sweep and an overlapping 34 ms tibial closure, rather than a slow straight-arm reach. These are animation choices informed by other mantis species, not measured European-mantis timings. Use **0.25×** or scrub the phase-labelled timeline to inspect the catch. Loop is off by default for Attack; it can be deliberately re-enabled. The approved Idle and Attack **bone keyframes** remain unchanged. All clips now also explicitly key the membrane morph (zero outside Stance), so imported wing fans do not remain open after changing clips.

## Realism and limitations

This is a **reference-informed, procedurally modelled anatomical study**, not a photogrammetric scan, motion capture, or a claim of film-ready photorealism. The walk represents one deliberately slow four-legged gait, not all mantis locomotion: forelegs can also participate in other walking contexts. Its precise footfall timing and the held threat pose are authored, not recorded from a living specimen. Fine anatomy and gait are approximations, and the species identification has not been reviewed by an entomologist. A final hero asset would still benefit from specialist sculpting, custom UV/texture painting, close-range anatomy review, retopology and engine-specific LOD work. Do not mistake a large triangle count for validated anatomical accuracy.

The viewer adds a subtle view-dependent compound-eye pseudopupil. The portable GLB keeps standard PBR eye materials and facet maps; custom shader logic is **not** embedded in glTF. All skeletal animation and wing-membrane morph animation are embedded. `Stance` starts in the raised pose and loops there; an engine should cross-fade into it rather than expect a neutral-to-display lead-in to be part of the clip.

## Source and reproducibility

- `src/mantis.js` — geometry, deterministic texture generation, skeleton and keyframe authoring.
- `src/strike-motion.js` — isolated, hinge-constrained strike curves, phase boundaries and timing.
- `src/walk-motion.js` — slow wave coordination and contact-speed-matched foot trajectories.
- `src/stance-motion.js` — held deimatic stance, limb spread and wing angles.
- `src/hindwing.js` — veined membrane texture and unfold morph, using the existing rig.
- `src/main.js` — Three.js lighting, wall-clock playback, camera and inspection tools.
- `scripts/build-asset.mjs` — exports the actual skinned model and clips from the generator, using a headless browser.
- `scripts/browser.mjs` — portable Chromium / software-WebGL setup for generation and checking.
- `scripts/import_into_blender.py` — optional local Blender importer that creates a `.blend` from the GLB.
- `references/README.md` — reference and photograph attribution notes.

To regenerate (with the dev server already running on port 5173):

```bash
node scripts/build-asset.mjs
node scripts/validate-asset.mjs
npm run test:viewer
npm run test:hunt
npm test
npm run build
```

The generated GLB is included because the requested deliverable is the usable 3D model. Dependencies, generated browser screenshots, and build output are ignored. No unrelated lobby/wallet patch has been applied.

## Separate neutral frog page

Open **`/frog.html`**, or run `npm run dev:frog` for a preview whose root redirects to the frog. The original mantis page remains at `/index.html`. Both HTML entry points are included by `npm run build`.

The frog is a neutral **Rana temporaria-inspired** 3D design with olive-brown/taupe skin, a pale underside and muted natural markings. The viewer includes three restrained palette previews, orbit/zoom, face/front/side/top cameras, wireframe, skeleton inspection and animation scrubbing.

- **Model:** `public/models/frog.glb` — metre-scale skinned GLB, 39 bones, embedded normal/roughness maps, `Idle` and `Blink` clips. The download is the original neutral base colourway; Stone and Olive are viewer tints.
- **Source:** `src/frog/model.js`, `implicit.js`, `appearance.js`, `mesh.js`. Main body and muscle volumes are joined into a continuous weighted skin surface rather than displayed as intersecting primitive objects.
- **Reference notes:** `references/FROG.md`.
- **Generation:** with the frog server on port 5174, `npm run frog:build`.
- **Checks:** `npm test` and `npm run test:frog`.

This is a detailed procedural design study, not a scan or a certified AAA production asset. The crab has a separate page at `/crab.html`; see below.

## Shore crab page and pincer controller

Open **`/crab.html`**, or run `npm run dev:crab` for the crab-focused preview on port 5175. The mantis and frog remain separate pages. Production builds include all three HTML entries.

- **Subject:** adult male *Carcinus maenas*, ~72 mm carapace width, four walking-leg pairs and two articulated chelipeds. Reference notes and limitations: `references/CRAB.md`.
- **Motion library:** `Idle`, `Walk_Left`, `Walk_Right`, `Pinch_L`, `Pinch_R`. Sideways walking uses an alternating tetrapod pattern with contact-matched foot trajectories. **A / D** or the arrow keys select left/right walking; Space pauses.
- **Pincer test:** drag the amber bead, or set Across / Forward / Height. Choose Auto, Left or Right. **Reach & grasp** opens the selected claw, reaches, closes its movable dactylus against the fixed propodal finger, checks contact, and lifts/holds the bead. **Release** and **Reset** repeat the test. Targets outside the arm workspace are rejected, and moving a target during closure can cause a miss.
- **Portable asset:** `public/models/crab.glb`, including five clips and embedded maps. The walks are **in-place** and expect **0.01 m/s lateral controller translation** at natural size. The scrolling reference grid is a tracking-view aid.
- **Runtime behavior:** `src/crab/rig.js` and `controller.js`. The interactive target controller is source code, not a behavior system embedded in GLB.
- **Regenerate / check:** `npm run crab:build` (crab server running), `npm test`, `npm run test:crab`, `npm run build`. Add `-- --screenshot` to the asset build for a generator screenshot.

This is a detailed, reference-informed **procedural prototype**, not a photogrammetric scan, measured motion capture or verified AAA hero asset. It needs specialist sculpt/texture review and engine-specific production work before that claim would be justified.
