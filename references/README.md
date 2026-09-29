# Mantis reference notebook

The base is an adult, green **European mantis (Mantis religiosa)**. Orchid-mantis petal anatomy from the earlier concept image is deliberately excluded.

## Photograph used for proportions

**Charles J. Sharp**, *European praying mantis (Mantis religiosa) green female, Dobruja, Romania*, 11 August 2022.

- Original and attribution: https://commons.wikimedia.org/wiki/File:European_praying_mantis_(Mantis_religiosa)_green_female_Dobruja.jpg
- License: **Creative Commons Attribution–ShareAlike 4.0** — https://creativecommons.org/licenses/by-sa/4.0/
- Local reference: `public/references/mantis-religiosa-charles-sharp.jpg`
- Changes: resized thumbnail; no content alterations.
- The photograph is displayed as an attributed reference only. It is not projected onto the model or used as a texture. The photo retains its own license.

## Anatomy and behaviour

1. European mantis species description and photographic references: [1](https://en.wikipedia.org/wiki/European_mantis).
   - Used for the adult female body plan, folded wings, triangular head, and proximal black/ivory forecoxal patches.
2. Amateur Entomologists' Society, *Raptorial*: [2](https://www.amentsoc.org/insects/glossary/terms/raptorial/).
   - Opposing grasping surfaces on different limb segments, not mammal-like hands.
3. Oufiero et al., *Patterns of variation in feeding strike kinematics of juvenile ghost praying mantis (Phyllocrania paradoxa): are components of the strike stereotypic?*, Journal of Experimental Biology 219 (2016): [3](https://journals.biologists.com/jeb/article/219/17/2733/15387/Patterns-of-variation-in-feeding-strike-kinematics).
   - Reference for approach, coxal/tibial extension, sweep, closure and retraction sequence. This is another mantis species, so it guides the mechanism rather than establishing exact M. religiosa timing.
4. *Roles of muscle activities in foreleg movements during predatory strike of the mantis*, Journal of Insect Physiology (2022): [4](https://www.sciencedirect.com/science/article/abs/pii/S0022191022001202).
   - Reference for coordinated coxa–trochanter and femur–tibia joints; the tarsus is not the primary prey-grasping mechanism.
5. Fedaro, *Mantis religiosa Head*, Wikimedia Commons, CC BY 4.0: [5](https://commons.wikimedia.org/wiki/File:Mantis_religiosa_Head.jpg).
   - Additional macro photograph identified for subsequent head-detail review. Not redistributed or used as texture.

## Scope

The model is authored from geometric surfaces and deterministic maps, not copied from a downloaded animal mesh. Animation is manually parameterised and baked, not measured motion capture. Anatomical counts, membrane thickness, pseudopupil optics, spine arrangement, and gait remain approximations requiring specialist review before any claim of scientific/film-level fidelity.

## Strike revision (v1.1)

The initial clip incorrectly held the femora nearly horizontal while extending the coxae, creating a straight-armed reach. The revision uses an elevated, folded approach and a fast femoral depression with overlapping tibial flexion, followed immediately by closed-gripper retraction towards the mouth. The long fore-tarsi fold back instead of acting like grasping fingers.

- Rossoni & Niven (2020), *Prey speed influences the speed and structure of the raptorial strike of a ‘sit-and-wait’ predator*: [6](https://pmc.ncbi.nlm.nih.gov/articles/PMC7280040/).
- The five high-speed stills labelled initial posture / approach / sweep (thrust) / sweep (capture) / retraction were inspected through the image-search reference from [7](https://www.livescience.com/mantis-strikes-deadly-precision-video.html). Those stills show the foreleg folding and capture sequence much more clearly than the original pose interpolation. The research and stills concern other mantis species, not measured motion capture of this asset's species.
- Direct video download was unavailable in this environment. The correction uses inspected high-speed **stills**, the published phase descriptions and anatomical hinge constraints; it does **not** claim frame-tracked video/motion-capture accuracy.

`src/strike-motion.js` documents the authored angles and event times. Regression tests verify closing during forward movement, positive gape throughout the sweep, fixed femur/tibia hinge planes, plantar anchors, retraction towards the head, seamless recovery, and unchanged Idle/Walk tracks. The 240 Hz Attack bake preserves the brief motion for engine import.

## Slow walk and threat stance (v1.2)

### Walking: confidence and scope

The four-support-leg choice is supported, but the original loop was not a measured mantis walk. It also used a middle-before-hind ordering on each side, and the planted-foot speed did not match the advertised controller speed. The revised clip is a conservative **slow wave**: hind then middle on one side, then the other side; one swinging leg at most; folded raptorial limbs; very small body/antenna movement.

- [8](https://journals.biologists.com/jeb/article/215/24/4255/11167/Quadrupedal-gaits-in-hexapod-animals-inter-leg), *Quadrupedal gaits in hexapod animals – inter-leg coordination in free-walking adult stick insects*, JEB 215 (2012): 4255. Its discussion specifically distinguishes mantis walking on the middle and hind legs from faster locomotion that may include the forelegs. It is **not** direct motion-capture data for this model.
- [9](https://journals.physiology.org/doi/full/10.1152/jn.00658.2017), *Six-legged walking in insects: how CPGs, peripheral feedback, and descending signals generate coordinated and adaptive motor rhythms*. Background on slow wave coordination and long stance durations; not a mantis-specific measurement.

The selected 2.4 s period, 80% support duty factor, 1.2 mm lift and 2.5 mm/s nominal travel speed are animation parameters, not claimed experimental measurements. Adding controller translation fixes the planted contacts in ground coordinates; without that translation, the exported loop remains an in-place study. The viewer's scrolling grid is explicitly a tracking-view aid.

### Stance: a defensive display, not a feeding strike

- [10](https://www.keepinginsects.com/praying-mantis/species/european-mantis/), *European Mantis — Mantis religiosa*. Species-specific inner forecoxal markings and wing-raising defensive display.
- [11](https://www.sciencephoto.com/media/367584/view/the-mantis-mantis-religiosa-intimidation-posture), *The mantis, Mantis religiosa: intimidation posture*. Species-specific description of raised forelegs, eyespot exposure and wing display.
- [12](https://gerbeaud.com/faune/mante-religieuse,1768.html), photographic posture reference, “Mante religieuse en posture d’intimidation” (photo marked © cdnh). Inspected as a reference; **not** redistributed as a texture or included in the public asset.

The clip holds a raised, spread-foreleg display with a more upright prothorax and all four supporting feet planted. Lifted green tegmina and a pale, veined hindwing fan are included; the conspicuous black hindwing patches of **Iris oratoria** are deliberately not copied onto M. religiosa. Small periodic motions make the hold loopable without repeatedly dropping the forelegs. The viewer cross-fades into the pose over ~0.65 s. The timing, amplitude and membrane folding are approximate animation/geometry choices, not species-validated capture.

The accepted Attack and Idle bone-keyframe hashes are regression-checked. Only Walk is revised; Stance and its wing morph are added.

## Interactive fixation / approach test (viewer v1.3)

- [13](https://www.amnh.org/explore/videos/biodiversity/mantises-predators), American Museum of Natural History, *How Mantises Became Nature’s Strangest Assassins*. The transcript describes visual tracking of offered prey, flexibility in aiming, and a mantis “inching in” before capture. It is useful behavioral context, not a numerical timing dataset.

The new controller is an **authored demonstration**, not a claim that every real strike uses an identical lock-and-lean sequence. It smooths head targeting, requires stable reachable placement, adds a tunable slow lean, then commits to a short strike. The grasp solves both forelegs around the actual bead position while preserving anatomical segment lengths and planted walking feet. A moved target can invalidate the committed catch.

Contact is checked using gripper geometry proxies rather than simply parenting the bead at a timer event. On success, its attachment offset preserves its world position and it follows the closed grippers during retraction. Release opens the tibiae before simple gravitational falling. This is not full prey-body, soft-tissue or collision physics. The accepted GLB clips remain unchanged; runtime logic is kept in separate `hunt-*` modules.
