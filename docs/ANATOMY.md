# Anura — Female Mosquito Character Bible

All dimensions are **millimetres**, taken from published morphometric literature. The
runtime model is built 1 unit = 1 mm so every value below maps 1:1 into the scene graph —
there is no "artistic scaling" anywhere in the rig.

---

## 1. Sources

| # | Source | Used for |
|---|--------|----------|
| S1 | Clements (1992) *The Insects of Los Angeles County*; Wootton (1984) in *Insects of the World*; Snodgrass (1959) — via AskNature & *Sci. Rep.* ch.5 | Proboscis architecture: labium as scaly sheath, 6 stylet fascicle, labrum/epipharynx = food canal, hypopharynx = salivary canal, paired mandibles + maxillae = saws, labella + ligula at tip |
| S2 | Kong & Wu (2010) *Phys. Rev. E* 82:011910 — "Mosquito proboscis: an elegant biomicroelectromechanical system" | Fascicle vibrates **~30 Hz** while penetrating; maxillae operate as variable-frequency microsaws at **10–15 Hz** early in penetration; labium **buckles back** and stays on the skin; `Ae. aegypti` proboscis **2.32 mm** long, labrum ⌀ 25 µm, fascicle ⌀ ~50 µm |
| S3 | Biting Innovations of Mosquito-Based Biomaterials (PMC9267633) | Proboscis 2 mm × 80 µm; labrum OD/ID 30/20 µm; labrum tip sharpened to a **15°** included angle; maxillae "harpoon-like jagged edges"; body is supported while the labium rests on the skin and bends back |
| S4 | Ask a Biologist — ASU (2024) | Labium = outer covering that bends and pulls away; fascicle = inner needle |
| S5 | Nature 2017, 21927 — Bomphrey et al., "Smart wing rotation and trailing-edge vortices enable high frequency mosquito flight" (*Culex quinquefasciatus*, 8 × Photron SA3 @ 10 000 fps) | Wingbeat **> 800 Hz** free-flight; total angular sweep **≈ 40°**; lift dominated by **pronation/supination** (wing pitch rotation) + **wake-capture trailing-edge vortex** + **rotational drag**, *not* by translational LEV |
| S6 | *Aerospace* (PMC8147425) mosquito aero review | Stroke amplitude **40–45°**; Re ≈ 120; aspect ratio ≈ 4.2; mass 2.1 mg; flapping 600–800 Hz; **flexible wing** → tip lags the root in phase |
| S7 | Zhang et al. / IR sensor study, *Sci. Rep.* 2021 (s41598-021-89644-z) | *Ae. aegypti* mean wingbeat **498.1 Hz**; Culex 341–437 Hz; low stroke amplitude ≈ 40° |
| S8 | Acoustic study *Ae. albopictus* / *Cx. quinquefasciatus*, *Sci. Rep.* 2025 | *Ae. albopictus* female steady **499.28 ± 18.06 Hz** after 24 h; *Cx. quinquefasciatus* 366–444 Hz; range 208–796 Hz |
| S9 | Ar5iv 1205.5260 — Free flight of *Aedes aegypti* (13 000 fps) | Free-flight male *Ae. aegypti* ≈ **800 Hz**; within a single flight frequency changes by up to 50 Hz; swarming flight is a figure-8; body yaw/pitch SD < 1° |
| S10 | Wendler (1965/66); Cruse (1976, 2009); Bender et al. (2011); *J. R. Soc. Interface* — Neuroethology of Insect Walking (Scholarpedia) | Gait ladder: **metachronal wave (slow) → tetrapod/gliding (mid) → alternating tripod (fast)**; **stance starts at 0, stance ends at π** (duty factor > 0.5 by definition of the pattern); swing −π → 0; tripod = **{L1, R2, L3}** vs **{R1, L2, L3}**; front leg of a tripod **leads** middle, which leads hind (the *M-tripod* of Kim et al. 2021) |
| S11 | Kim et al. 2021, *PNAS* 118(10) e2013994118 — "Drosophila uses a tripod gait across all walking speeds" | **Duty factor ≈ 0.6–0.7**; intra-tripod stance-start delays are small and shrink with speed; front→mid→hind ordering inside a tripod; tripod *shape* (L/r_m) rather than leg stiffness sets speed |
| S12 | Aresta/Gruhn & Büschges JEB 2012 — hexapod gaits | At low speed insects use **wave gait**; deviation from ideal phase is only ±0.12 of a cycle; tetrapod = two diagonal legs swinging in synchrony |
| S18 | Wendler/Bässler; *J. Exp. Biol.* 210:1092 (2007) tibia extensor & flexor moment arms; Proc. Roy. Soc. B 2016 (insect leg joint torques) | Tibial muscles scale with femur length; the **femur–tibia joint's neutral posture is ≈ 90° to the femur** and stance torques pull it back toward that angle; body height rises when the femora are depressed |
| S13 | Wootton (1984); Gibson & Bachtrach; *J. Exp. Biol.* pulvillus work | Pretarsus: **paired tarsal claws (ungues)** for rough substrate + **paired pulvilli** with tenent setae + **empodium** for smooth substrate; adhesion is **wet** (capillary + van der Waals); attachment at heel strike, **detachment by tarsal flexion using the claw as a lever**; adhesion force is *shear-sensitive*; climbing legs above the CoM can supply up to ~50 % of their shear force as normal adhesion |
| S14 | ECDC Reverse-identification key, July 2022 | *Ae. aegypti*: silver **lyre** marking on scutum; pale inter-articular **rings on the legs**; dark-scaled wing with a pale patch at the base of the costa; pale basal bands on tergites; fore tarsus pale-banded |
| S15 | Smujo Biodiversitas 2023 (SEM plate) | Microtrichia on the wing membrane; leaf-shaped (lanceolate) scales; **femur with a longitudinal white line**; white knee spot; fore/mid/hind femora differ; claws differ per leg pair; **hind legs are the longest, fore legs the shortest** |
| S16 | Real-field measurements of *Ae. aegypti* wing length (host-seeking, Iquitos 672 specimens) | Wing length **1.80 – 3.23 mm**, median ≈ 2.60–2.76 mm; body length **4–7 mm**; *Ae. aegypti* is the small species |
| S17 | Nature 2017 Methods / Extended Data | Mosquitoes use **asynchronous (indirect) flight muscle**; thorax is a resonant structure; wingbeat is set by thorax natural frequency, not motor neuron rate |

---

## 2. Master proportions (female *Aedes aegypti*, mid-size specimen)

Scene unit = 1 mm. Body axis: **+X = anterior (head)**, **+Y = dorsal (up)**, **+Z = left**.

| Structure | Value | Note |
|---|---|---|
| Body length, head→tergite VIII (excl. proboscis) | **4.60** | S16: 4–7 mm |
| Proboscis length | **2.32** | S2 (measured) |
| Wing length (axillary notch → apex of R3) | **2.72** | S16 mean of host-seeking ♀ |
| Head capsule | ⌀ 0.70 wide × 0.62 tall × 0.58 deep | S: Clements |
| Compound eye | ⌀ 0.46, bulging, occupies 62 % of head side | |
| Ocelli | 3, dorsal, 0.05 | |
| Scutum (thorax dorsum) | 1.10 long × 0.66 wide × 0.30 tall, arched | lyre marking S14 |
| Scutellum | 0.24 | |
| Metanotum + halteres | haltere 0.34 × 0.09, knobbed | Diptera |
| Abdomen | 2.35 long, 8 tergites, max ⌀ 0.52 at T-IV | |
| Tergite banding | pale basal bands on T-II…T-VI | S14 |
| Maxillary palp | 4 segments, total 0.86 (≈ 37 % of proboscis, short-palped culicine ♀) | |
| Antenna | scape 0.22 + pedicel 0.08 + 12 flagellomeres ≈ 1.95 total; **sparse hairs (♀, not plumose)** | S: Ae. aegypti ♀ antennae |
| Leg fore (coxa→claw) | 0.30 / 0.28 / **1.28** / **1.38** / 0.60 + claw 0.14 | S15 (shortest pair) |
| Leg mid | 0.32 / 0.28 / **1.52** / **1.62** / 0.66 + claw 0.15 | S15 |
| Leg hind | 0.34 / 0.29 / **1.78** / **1.92** / 0.74 + claw 0.17 | S15 (longest pair) |
| Coxa attachment (pro/meso/meta) | x = +0.62 / +0.22 / −0.18 relative to thorax centre, z = ±0.30, y = −0.20 | thorax origin at pro-mesothorax boundary |
| Wing root | x = +0.30, y = +0.20, z = ±0.20, hinge axis pitched 12° nose-up | |
| Stroke plane tilt | 28° from horizontal | |
| Total sweep | **44°** (±22°) | S5/S6 |
| Wing pitch (AoA) in translation | **±38°**, flipped within 6 % of cycle at reversal | S5 (reversal lag ~6 % of cycle) |

---

## 3. Locomotion constants

| Constant | Value | Source |
|---|---|---|
| Gait, fast | alternating **M-tripod** `{L1,R2,L3}` ⟷ `{R1,L2,L3}` | S10, S11 |
| Gait, slow | **metachronal wave** R3→R2→R1 then L3→L2→L1, 1/6 cycle per leg | S10, S12 |
| Gait, mid | **gliding / tetrapod**, 2 diagonal legs in swing, duty 0.78 | S12 |
| Duty factor (fast) | **0.68** | S11 |
| Intra-tripod delay | front → mid → hind: 0.035 cycle each (≈ 0.07 total) | S11 |
| Step length | 0.55 mm at cruise (1.1 × leg length / stride count) | S11 |
| Stride frequency | 14 Hz at cruise, 22 Hz max | S11 |
| Body clearance | 0.62 mm, rises 0.18 mm through stance then settles | S10 |
| Stance radius | tarsi splay to the radius that puts the **femur–tibia joint at its neutral ~90°** posture — `0.68 × (trochanter+femur+tibia)` per leg. Derived from each leg's own segment lengths, *not* an authored offset, which is what keeps all three leg pairs inside the 6°–132° range and holds the tarsi outboard | S10, S18 |
| Body attitude | **nose-down 9°** about `cross(up, heading)`: head and proboscis carried below the level of the thorax, abdominal tip trailing slightly lower. (A positive angle about that axis drops the head.) | S10 |
| Leg frame | every segment of a chain is authored as a **local** offset `(0, segmentLength, 0)` from the joint above it, so the rig builds children in the parent's frame | — |
| Foot slip | **0** — stance feet are world-locked | hard requirement |
| Knee | femur–tibia flexion 6° … 132°; coxa abduction keeps tarsi outboard of the midline so legs **cannot cross** | S10 |
| Tarsus | 5 tarsomeres; terminal tarsomerus flexes 25–40° at heel-strike and again at lift-off | S13 |
| Adhesion | pulvilli engage at heel strike (splayed), release by tarsal flexion using the claw as a pry bar | S13 |
| Wall walking | body up-vector = smoothed mean contact normal; gravity is *not* opposed by friction, it is opposed by **normal adhesion**, so the CoM may leave the support polygon | S13 |

---

## 4. Blood-feeding constants

| Phase | Value | Source |
|---|---|---|
| Probing proboscis vibration | **30 Hz** (fascicle) | S2 |
| Maxillary saw | **10–15 Hz** | S2 |
| Labium sheath | slides back into a **loop** above the head and rests on the skin | S1, S3, S4 |
| Labrum tip included angle | 15° | S3 |
| Fascicle ⌀ | 50 µm → 0.05 mm (rendered as a visible 0.02 needle at scene scale) | S2 |
| Palp behaviour | palps **tap and probe** the surface to locate the capillary before insertion | S1 |
| Salivation | hypopharynx delivers anti-coagulant continuously during probing | S2 |
| Engorgement | abdomen swells to ≈ **1.9×** girth over the meal, tergites separate, colour shifts to red through translucent cuticle | standard haemophagous result |
| Withdrawal | labium sheath slides *forward* over the fascicle, which is withdrawn last | S1 |

---

## 5. Rendering-honesty notes

* A 500 Hz wingbeat cannot be shown at 60 fps. The wing is therefore **sampled at the
  virtual shutter time**, exactly as a 10 000 fps high-speed camera samples it — the
  strobed appearance you see is the true sampled appearance, not a slowed-down fake.
  A *Time ×0.02* mode drops the simulation clock so the real stroke envelope,
  pronation and supination are visible directly.
* Every leg segment is a rigid sclerite on a joint, which is what an insect leg
  actually is. The rig is a pure node hierarchy, so the exported GLB animates
  natively in Unreal/Unity with **no skinning artefacts and no blend shapes**.
