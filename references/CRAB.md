# Shore crab: reference and implementation notes

## Subject

**Carcinus maenas**, an adult male European shore/green crab, approximately 72 mm carapace width. The model is an original procedural surface asset, not a photogrammetric specimen or a claimed AAA production asset.

## Anatomy references

1. [MarLIN — Common shore crab](https://www.marlin.ac.uk/species/detail/1497): broad carapace, five anterolateral teeth on each side, three frontal lobes, variable green/brown colour and developed chelae.
2. [Smithsonian SERC — Carcinus maenas](https://invasions.si.edu/nemesis/species_summary/98734): finely granular shell, short merus, carpal tooth, smooth walking legs, slightly flattened hind legs rather than swimming paddles, and folded male abdomen.
3. [Alaska Department of Fish and Game — identification](https://www.adfg.alaska.gov/index.cfm?adfg=invasiveprofiles.europeangreencrab_characteristics): four walking-leg pairs plus one claw pair, diagnostic shell teeth.
4. [Walla Walla University — photographs](https://inverts.wallawalla.edu/Arthropoda/Crustacea/Malacostraca/Eumalacostraca/Eucarida/Decapoda/Brachyura/Family_Portunidae/Carcinus_maenas.html): inspected dorsal, ventral and limb reference photos. The live individual shown is missing legs; those missing limbs were NOT reproduced. White surface frost on the frozen specimen was NOT treated as live pigmentation.
5. [Reef Life Survey — live crab photographs](https://reeflifesurvey.com/species/carcinus-maenas/): inspected live olive/umber mottling and shell proportions. Photos are reference only, not redistributed as textures.
6. [Crab Museum — The Claw](https://www.crabmuseum.org/blog/the-claw): fixed propodal finger and one movable dactylus, not two independently hinged mechanical fingers.
7. [Claw morphology and feeding rates](https://www.researchgate.net/publication/232684626_Claw_Morphology_and_Feeding_Rates_of_Introduced_European_Green_Crabs_Carcinus_maenas_L_1758_and_Native_Dungeness_Crabs_Cancer_magister_Dana_1852): larger right crusher and smaller left cutter as represented by this specimen. Handedness is not asserted to be universal across all individuals.

## Movement references

8. [Schreiner, crab locomotion thesis, LSU](https://repository.lsu.edu/gradschool_theses/1349/): lateral walking in the studied crab species, including shore crabs, predominantly used an alternating tetrapod gait. Leading/trailing legs have different mechanics; the propodus–dactyl joints of shore crabs can act as supporting struts.
9. [Barnes, 1975, Uca pugnax](https://link.springer.com/article/10.1007/BF00612697): alternating tetrapod coordination and leading/trailing differences. This is cross-species context, not a measured C. maenas motion dataset.

The chosen 1.6 s gait period, 64% stance duty factor and 10 mm/s speed are **authored values**, not claimed measurements. The clips are in-place, with foot trajectories matched to the opposite of lateral controller speed. The viewer scrolls its ground reference at that speed; it is an explicitly labelled tracking view, not exported root motion.

## Grasp controller

The cheliped uses fixed-length arm segments and an independently hinged dactylus. The lower finger remains part of the propodus. A target position defines the desired palm frame and wrist position; two-link IK solves the arm. A closure angle is found against a sampled curved-finger contact proxy. Both the fixed and moving finger are checked against the actual target before capture. Moving the bead away during closing can produce a miss. Successful grasping preserves the bead's attachment position, lifts it with the claw and releases it before a simple gravitational drop.

These are analytic kinematic/contact proxies, not full rigid-body, cuticle, tendon or prey-deformation physics. Dynamic control logic lives in `src/crab/controller.js` and `rig.js`; it is not embedded into the GLB.

## Fidelity status

The asset has the intended body plan, skeletal motion and portable clips, but **is not certified AAA / film-ready photorealism**. A production hero asset would still need specialist sculpting, specimen-based texture authoring, biological review, optimized topology/LODs and target-engine QA. No scan, license or measurement has been fabricated. A Smithsonian CC0 blue-crab scan was identified in the earlier research but was not downloaded or used; it is also a different species.
