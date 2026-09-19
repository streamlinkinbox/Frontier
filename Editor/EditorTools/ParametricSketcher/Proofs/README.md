# SolidArc proof renders

Every SolidArc verification executable renders its own 2560 × 1600 contact sheet (or 1280 × 800 view) into this folder
through the software raster, and the render is deterministic: re-running a suite reproduces the committed PNG byte for
byte (verified across all 151 renders of the `SultanAladin/Frontier-` @ `arena/01a0b490-frontier` tree, commit
`5a6d1a8`).

The complete historical set (Phases 2 – 32y, ≈ 203 MB) therefore lives in that source-of-truth tree and is **not**
duplicated here. To reproduce any of them locally:

```
bash Scratchpad/BuildSolidArc.sh            # g++ build of SolidArc + every verification binary → /tmp/solidarc-build
/tmp/solidarc-build/<Suite>Verification     # e.g. PlaneCylinderFilletVerification → Proofs/Phase31_PlaneCylinderFillet.png
```

`.gitignore` excludes `Proofs/*.png` by default so a full suite run does not flood `git status`; the proof of each new
phase completed in this repository is committed explicitly (`git add -f Proofs/<Phase>.png`) and un-ignored by name.
